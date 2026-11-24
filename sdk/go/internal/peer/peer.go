// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package peer is the one seam between the SDK's cross-implementation suite
// and the firmware it talks to.
//
// By default the peer is tfwsim, the firmware compiled for the host, launched
// through simproc. When PortEnv names a serial port the peer is a real board on
// that port instead (`make hw-test`), BoardEnv says which board it is, and
// ResetEnv how it is reset: DTR by default, QMP for a board QEMU emulates. The
// suite itself is the same either way: it gets an io.ReadWriteCloser and does
// not know which one it has. An Ethernet row also listens where
// Target.Listen says, and the device -- the simulator, or the board EthEnv
// names the address of -- dials it.
//
// A board that is missing or does not answer fails the test. There is no
// fallback to the simulator and no skip: a hardware run that quietly tested
// the simulator would report a board as conformant that was never spoken to.
package peer

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/netip"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

const (
	// PortEnv names the serial port of a real board. Unset, the peer is tfwsim.
	PortEnv = "TFW_HW_PORT"
	// BoardEnv names the board on PortEnv, as boards/<board>/ does.
	BoardEnv = "TFW_HW_BOARD"
	// EthEnv names the ip:port the board's network block dials for its
	// Ethernet link. A board run that leaves it unset runs no Ethernet row.
	EthEnv = "TFW_HW_ETH"
	// EthListenEnv names the ip:port this host listens on for that link, when
	// it is not EthEnv's: a suite behind a NAT or a published port listens on
	// its own address, a wildcard included, while the board dials the bench
	// host's. Unset, the suite listens on EthEnv's.
	EthListenEnv = "TFW_HW_ETH_LISTEN"
	// ResetEnv names how the board on PortEnv is reset before each test.
	// Unset, by pulsing DTR, as avrdude does: every AVR board. "qmp:<socket>"
	// sends QMP system_reset to the QEMU whose monitor listens on that Unix
	// socket, for an emulated board whose port is a pty, which has no modem
	// lines (`make integration-test-cortex-m-wire`).
	ResetEnv = "TFW_HW_RESET"
	// SimListen is where the simulator's Ethernet link is listened for: a
	// loopback port the kernel picks, which the test writes into the
	// simulator's network block.
	SimListen = "127.0.0.1:0"
	// SimBoard is the board profile tfwsim is compiled against: HOST_BOARD in
	// mk/host.mk.
	SimBoard = "atmega328p"
	// StartBound is how long a board has, from its reset, to transmit its
	// first byte. The stock bootloader runs for about a second before the
	// application starts.
	StartBound = 5 * time.Second
)

// boardStartBound is a board's bound from its reset to its first byte where
// StartBound is too short. The STM32H573I-DK's tfw-boot checks the
// application's P-256 signature in software at 64 MHz: about 16 s from the
// start of the probe's reset to the BOOT event, measured on the board
// 2026-11-17, so it gets 30 s.
var boardStartBound = map[string]time.Duration{
	"stm32h573i-dk": 30 * time.Second,
}

// Bound is how long t's board has, from its reset, to transmit its first
// byte: its boardStartBound, or StartBound.
func (t Target) Bound() time.Duration {
	if b, ok := boardStartBound[t.Board]; ok {
		return b
	}
	return StartBound
}

// ErrSilent reports a board that transmitted nothing within the bound.
var ErrSilent = errors.New("the board sent nothing after its port opened -- is the firmware flashed, and is this its port?")

// Target is the peer the suite talks to.
type Target struct {
	Port      string // empty for the simulator
	Board     string
	Eth       string // where the board dials; empty for the simulator and a serial-only run
	EthListen string // where the suite listens for it, when not Eth; empty for Eth
	QMP       string // the QMP socket that resets the board; empty resets it by DTR
	// ResetCmd is the program and its arguments, separated by spaces, that
	// reset the board through its debug probe, for a board whose serial
	// line carries DTR to no reset (the ST-LINK VCOM); empty for DTR or QMP.
	ResetCmd string
	Hook     string // the command that resets a TCP board (cmd:); empty for none
}

// Hardware reports whether the target is a real board.
func (t Target) Hardware() bool { return t.Port != "" }

// Resets reports whether Start resets the peer, so it sends BOOT: the
// simulator starts fresh, and a serial board is reset by DTR or QMP. A board
// whose wire is TCP is found running, as it is, and sends no BOOT; a device
// on it must not wait for one. StartBooted resets such a board through its
// Hook.
func (t Target) Resets() bool { return !IsTCP(t.Port) }

// NoResetOnOpen is why StartBooted skips a row on a TCP board with no Hook.
const NoResetOnOpen = "transport does not reset on open"

// ResetCmdPrefix marks a ResetEnv that is a TCP board's reset hook: the
// command, split on spaces, that resets the board (a relay, a probe's reset
// line, a power switch), e.g. cmd:/usr/local/bin/bench-reset p4.
const ResetCmdPrefix = "cmd:"

// Listen returns the address the Ethernet rows listen on for the device to
// dial: SimListen for the simulator; for a board, EthListenEnv's, or EthEnv's
// when that is unset. A board run that names no endpoint fails the row instead
// of listening where no board will ever dial. The listen address may be a
// wildcard, but not port 0: the board dials a fixed port, which the suite (or
// the port published to it) must be listening on.
func (t Target) Listen() (string, error) {
	if !t.Hardware() {
		return SimListen, nil
	}
	if _, err := t.Endpoint(netip.AddrPort{}); err != nil {
		return "", err
	}
	if t.EthListen == "" {
		return t.Eth, nil
	}
	ap, err := netip.ParseAddrPort(t.EthListen)
	if err != nil {
		return "", fmt.Errorf("%s=%s: %w", EthListenEnv, t.EthListen, err)
	}
	if ap.Port() == 0 {
		return "", fmt.Errorf("%s=%s: the board dials a fixed port, so the suite cannot listen on port 0", EthListenEnv, t.EthListen)
	}
	return t.EthListen, nil
}

// Endpoint returns the endpoint the device's network block names: bound, the
// address the simulator's listener was given, for the simulator; EthEnv's for
// a board, which is what its provisioned block dials whatever the suite
// listens on. A board's endpoint must be an address it can dial: a port, and
// not a wildcard, which belongs in EthListenEnv.
func (t Target) Endpoint(bound netip.AddrPort) (netip.AddrPort, error) {
	if !t.Hardware() {
		return bound, nil
	}
	if t.Eth == "" {
		return netip.AddrPort{}, fmt.Errorf("%s=%s: the Ethernet rows need the ip:port the board dials, and %s names none", PortEnv, t.Port, EthEnv)
	}
	ap, err := netip.ParseAddrPort(t.Eth)
	if err != nil {
		return netip.AddrPort{}, fmt.Errorf("%s=%s: %w", EthEnv, t.Eth, err)
	}
	if ap.Port() == 0 {
		return netip.AddrPort{}, fmt.Errorf("%s=%s: the board dials a fixed port, so it cannot be 0", EthEnv, t.Eth)
	}
	if ap.Addr().IsUnspecified() {
		return netip.AddrPort{}, fmt.Errorf("%s=%s: the board cannot dial a wildcard; name the address it dials, and put the wildcard in %s", EthEnv, t.Eth, EthListenEnv)
	}
	return ap, nil
}

// FromEnv reads the target from PortEnv and BoardEnv. A port without a board,
// or a board without a port, is an error rather than a guess: the board decides
// what the suite expects of the pins, and a board named with no port would
// otherwise run the simulator under a hardware label.
func FromEnv() (Target, error) {
	port, board, eth, listen := os.Getenv(PortEnv), os.Getenv(BoardEnv), os.Getenv(EthEnv), os.Getenv(EthListenEnv)
	reset := os.Getenv(ResetEnv)
	switch {
	case port == "" && reset != "":
		return Target{}, fmt.Errorf("%s=%s names how to reset a board but %s names no port", ResetEnv, reset, PortEnv)
	case port == "" && eth != "":
		return Target{}, fmt.Errorf("%s=%s names a board's Ethernet link but %s names no port: every Ethernet row starts the board through its serial port", EthEnv, eth, PortEnv)
	case port == "" && listen != "":
		return Target{}, fmt.Errorf("%s=%s names where to listen for a board but %s names no port: the simulator is always listened for on %s", EthListenEnv, listen, PortEnv, SimListen)
	case eth == "" && listen != "":
		return Target{}, fmt.Errorf("%s=%s names where to listen but %s names no endpoint for the board to dial", EthListenEnv, listen, EthEnv)
	case port == "" && board == "":
		return Target{Board: SimBoard}, nil
	case port == "":
		return Target{}, fmt.Errorf("%s=%s names a board but %s names no port", BoardEnv, board, PortEnv)
	case board == "":
		return Target{}, fmt.Errorf("%s=%s names a port but %s names no board", PortEnv, port, BoardEnv)
	}
	qmp, cmd, err := parseReset(reset)
	if err != nil {
		return Target{}, err
	}
	t := Target{Port: port, Board: board, Eth: eth, EthListen: listen, QMP: qmp, ResetCmd: cmd}
	if !t.Resets() {
		t.Hook = cmd
	}
	return t, nil
}

// parseReset reads ResetEnv: empty for DTR, the socket of "qmp:<socket>", or
// the hook of "cmd:<command>".
// Anything else is refused rather than read as DTR, which would reset nothing
// on the board the value was meant for.
func parseReset(v string) (qmp, cmd string, err error) {
	if v == "" {
		return "", "", nil
	}
	if line, ok := strings.CutPrefix(v, ResetCmdPrefix); ok {
		if cmd = strings.Join(strings.Fields(line), " "); cmd != "" {
			return "", cmd, nil
		}
	} else if sock, ok := strings.CutPrefix(v, "qmp:"); ok && sock != "" {
		return sock, "", nil
	}
	return "", "", fmt.Errorf("%s=%s: want qmp:<QMP socket path>, %s<command> that resets the board, or unset to reset by DTR", ResetEnv, v, ResetCmdPrefix)
}

// Start connects to the target FromEnv names: sim, run with args, when it is
// the simulator, otherwise the board on the port. args are the simulator's
// flags; a board has none, it runs what was flashed with the EEPROM it was
// provisioned with. The connection is closed when the test ends; anything that
// stops it being made fails the test.
func Start(tb testing.TB, sim simproc.Binary, args ...string) io.ReadWriteCloser {
	tb.Helper()
	t, err := FromEnv()
	if err != nil {
		tb.Fatal(err)
	}
	if !t.Hardware() {
		return simproc.Start(tb, sim, args...)
	}
	c, err := openTarget(t)
	if err != nil {
		tb.Fatal(err)
	}
	tb.Cleanup(func() { _ = c.Close() })
	return c
}

// openTarget opens a hardware target the way its reset asks. A tcp: board
// reset by a cmd: hook gets TCPResetBound: the reset, the reboot and the
// lease all fall within it, which the board's serial start bound does not
// cover (an ESP32-P4's 5 s against a 6 s DHCP re-lease).
func openTarget(t Target) (io.ReadWriteCloser, error) {
	open := Open
	bound := t.Bound()
	if t.QMP != "" {
		open = func(port string, bound time.Duration) (io.ReadWriteCloser, error) {
			return OpenQMP(port, t.QMP, bound)
		}
	}
	if t.ResetCmd != "" {
		open = func(port string, bound time.Duration) (io.ReadWriteCloser, error) {
			return OpenCmd(port, strings.Fields(t.ResetCmd), bound)
		}
		if IsTCP(t.Port) && bound < TCPResetBound {
			bound = TCPResetBound
		}
	}
	return open(t.Port, bound)
}

// openPort is serialport.Open, swapped by tests: a port that resets and boots
// a board is a board, and the unit tests have none.
var openPort = serialport.Open

// Open opens a board's serial port, resets the board, and returns once it has
// started: when it transmits its first byte, which the application does with
// its BOOT event. That byte is kept for the caller's first Read, so the BOOT
// event is delivered exactly as tfwsim delivers it. Nothing is written before
// then, so no request lands in the bootloader.
func Open(port string, bound time.Duration) (io.ReadWriteCloser, error) {
	if addr, ok := strings.CutPrefix(port, TCPListenPrefix); ok {
		// A board that dials the host (the STM32H573I-DK's ETH_MAC=1
		// image): found running, as it is, on its next dial.
		return openListen(port, addr, nil, bound)
	}
	if addr, ok := strings.CutPrefix(port, TCPPrefix); ok {
		// A board whose wire is a TCP server (the ESP32 port, hal_uart.c):
		// no DTR to reset it, so the rows find it running, as it is.
		c, err := openTCP(addr, bound)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", port, err)
		}
		return c, nil
	}
	p, err := openPort(port, 0)
	if err != nil {
		return nil, err
	}
	c, err := start(p, bound)
	if err != nil {
		_ = p.Close()
		return nil, fmt.Errorf("%s: %w", port, err)
	}
	return c, nil
}

// TCPPrefix marks a PortEnv that is a board's wire endpoint, host:port,
// rather than a serial device: tcp:192.0.2.80:21574.
const TCPPrefix = "tcp:"

// TCPListenPrefix marks a PortEnv that is an address the host listens on for
// a board that dials out: the STM32H573I-DK's ETH_MAC=1 image dials its
// network block's endpoint and listens on nothing (#584), so its hw-test
// runs with PORT=tcp-listen:<host-port> (every interface) or
// tcp-listen:<host-address>:<host-port>, and each open takes the board's
// next connection.
const TCPListenPrefix = "tcp-listen:"

// IsTCP reports whether port is a board's wire over TCP, dialled (tcp:) or
// accepted (tcp-listen:): no DTR resets such a board.
func IsTCP(port string) bool {
	return strings.HasPrefix(port, TCPPrefix) || strings.HasPrefix(port, TCPListenPrefix)
}

// listenTCP binds the host's end of a tcp-listen: port. A bare port binds
// every interface.
func listenTCP(addr string) (*net.TCPListener, error) {
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}
	l, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	return l.(*net.TCPListener), nil
}

// acceptTCP takes the first connection the board opens on l before deadline
// and closes l: the board's later dials are refused, and redialled, until the
// next open listens again.
func acceptTCP(l *net.TCPListener, deadline time.Time) (net.Conn, error) {
	defer func() { _ = l.Close() }()
	if err := l.SetDeadline(deadline); err != nil {
		return nil, err
	}
	c, err := l.Accept()
	if err != nil {
		return nil, fmt.Errorf("no connection from the board: %w", err)
	}
	return c, nil
}

// listenDrained, when set, is called by openListen once it has closed the
// connections queued before the reset returned; nil in production. A test
// sets it so the rebooted board dials after that drain, not after a sleep.
var listenDrained func()

// openListen listens on addr, runs reset (if any) once the host is
// listening, so the rebooted board's first dial lands, and accepts the
// board's connection, all within bound.
func openListen(port, addr string, reset func() error, bound time.Duration) (io.ReadWriteCloser, error) {
	deadline := time.Now().Add(bound)
	l, err := listenTCP(addr)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", port, err)
	}
	if reset != nil {
		if err := reset(); err != nil {
			_ = l.Close()
			return nil, fmt.Errorf("%s: resetting the board: %w", port, err)
		}
		// The listener was up before the reset: a connection queued on it
		// by now is the board's pre-reset socket. Close each one; the
		// rebooted board dials again (and redials one closed here).
		if err := drainAccepted(l); err != nil {
			_ = l.Close()
			return nil, fmt.Errorf("%s: %w", port, err)
		}
		if listenDrained != nil {
			listenDrained()
		}
	}
	c, err := acceptTCP(l, deadline)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", port, err)
	}
	return c, nil
}

// tcpDrain is how long drainAccepted waits for each connection already
// queued on a listener: a queued connection is accepted at once.
const tcpDrain = 20 * time.Millisecond

// drainAccepted closes every connection already queued on l.
func drainAccepted(l *net.TCPListener) error {
	for {
		if err := l.SetDeadline(time.Now().Add(tcpDrain)); err != nil {
			return err
		}
		c, err := l.Accept()
		if err != nil {
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				return nil
			}
			return err
		}
		_ = c.Close()
	}
}

// tcpDials bounds the dials openTCP makes to a board that closes each one.
const tcpDials = 3

// tcpSettle is how long openTCP waits for a board to close a connection it
// refused. The board speaks only when asked, so a silent wire is a kept one.
const tcpSettle = 100 * time.Millisecond

// openTCP dials addr and returns the connection the board kept. A second
// guard behind hal_uart.c's handoff: a board that still holds the last row's
// socket closes the next one at once, after the dial has succeeded, so a
// connection the board closes inside tcpSettle is dialled again.
func openTCP(addr string, bound time.Duration) (io.ReadWriteCloser, error) {
	return openTCPWithin(addr, func() (time.Duration, error) { return bound, nil })
}

// openTCPWithin is openTCP with each dial's bound taken from within, which
// fails when no time is left, so a caller holding one absolute deadline
// never starts a dial it cannot finish.
func openTCPWithin(addr string, within func() (time.Duration, error)) (io.ReadWriteCloser, error) {
	var err error
	for range tcpDials {
		var bound time.Duration
		if bound, err = within(); err != nil {
			return nil, err
		}
		var c io.ReadWriteCloser
		c, err = dialTCP("tcp", addr, bound)
		if err != nil {
			return nil, err
		}
		nc, ok := c.(net.Conn)
		if !ok {
			return c, nil
		}
		if err = nc.SetReadDeadline(time.Now().Add(tcpSettle)); err != nil {
			_ = nc.Close()
			return nil, err
		}
		var b [1]byte
		n, rerr := nc.Read(b[:])
		if n > 0 {
			_ = nc.SetReadDeadline(time.Time{})
			return &primed{Conn: nc, first: b[:1]}, nil
		}
		var ne net.Error
		if errors.As(rerr, &ne) && ne.Timeout() {
			if err = nc.SetReadDeadline(time.Time{}); err != nil {
				_ = nc.Close()
				return nil, err
			}
			return nc, nil
		}
		_ = nc.Close()
		err = fmt.Errorf("the board closed the connection: %w", rerr)
	}
	return nil, err
}

// primed is a connection whose first byte openTCP read while it waited.
type primed struct {
	net.Conn
	first []byte
}

func (p *primed) Read(b []byte) (int, error) {
	if len(p.first) > 0 && len(b) > 0 {
		n := copy(b, p.first)
		p.first = p.first[n:]
		return n, nil
	}
	return p.Conn.Read(b)
}

// dialTCP is net.DialTimeout, a variable so a test can see the address. The
// conn keeps a read floor (floorConn), as serialport.Open's does.
var dialTCP = func(network, addr string, bound time.Duration) (io.ReadWriteCloser, error) {
	c, err := net.DialTimeout(network, addr, bound)
	if err != nil {
		return nil, err
	}
	return &floorConn{Conn: c, floor: tcpReadFloor}, nil
}

// tcpReadFloor bounds every Read on a tcp: conn, the floor serialport.Open's
// 200ms read timeout gives a serial PORT.
const tcpReadFloor = 200 * time.Millisecond

// floorConn is a TCP conn whose every Read returns within floor, or by the
// caller's own read deadline if that is sooner, with the timeout error tfw's
// read loop treats as "no bytes yet". tfw bounds a read only through
// SetReadDeadline or such a floor, and a row that wraps the conn as a plain
// io.ReadWriter (the CAPS row's meddler) hides SetReadDeadline: on a tcp:
// board, found running with no BOOT event to come, the read then blocked
// until the test binary's timeout (#551).
type floorConn struct {
	net.Conn
	floor time.Duration

	mu       sync.Mutex
	deadline time.Time // the caller's read deadline; zero is none
}

func (c *floorConn) Read(p []byte) (int, error) {
	c.mu.Lock()
	at := time.Now().Add(c.floor)
	if !c.deadline.IsZero() && c.deadline.Before(at) {
		at = c.deadline
	}
	c.mu.Unlock()
	if err := c.Conn.SetReadDeadline(at); err != nil {
		return 0, err
	}
	return c.Conn.Read(p)
}

func (c *floorConn) SetReadDeadline(t time.Time) error {
	c.mu.Lock()
	c.deadline = t
	c.mu.Unlock()
	return nil
}

func (c *floorConn) SetDeadline(t time.Time) error {
	_ = c.SetReadDeadline(t)
	return c.SetWriteDeadline(t)
}

// OpenQMP opens port, resets the board through the QEMU monitor on socket
// (qmpReset) and waits up to bound for it to transmit, as Open does after its
// DTR pulse. It never touches a modem line: port is QEMU's -serial pty, and a
// pseudo-terminal has none.
func OpenQMP(port, socket string, bound time.Duration) (io.ReadWriteCloser, error) {
	p, err := openPort(port, 0)
	if err != nil {
		return nil, err
	}
	c, err := startQMP(p, socket, bound)
	if err != nil {
		_ = p.Close()
		return nil, fmt.Errorf("%s: %w", port, err)
	}
	return c, nil
}

// purgeable is a port whose receive buffer can be emptied.
type purgeable interface {
	io.ReadWriteCloser
	ResetInputBuffer() error
}

// startQMP is OpenQMP after the open: drop what the old application sent,
// reset by QMP, then the same first-transmission wait as start.
func startQMP(p io.ReadWriteCloser, socket string, bound time.Duration) (io.ReadWriteCloser, error) {
	r, ok := p.(purgeable)
	if !ok {
		return nil, errors.New("the port cannot purge its receive buffer")
	}
	// Before the reset, not after as start does: qmpReset returns on QEMU's
	// RESET event, and from then on every byte is the new application's.
	// What is purged is the old one's, such as the BOOT event it sent when
	// QEMU started it.
	if err := r.ResetInputBuffer(); err != nil {
		return nil, fmt.Errorf("purging the receive buffer: %w", err)
	}
	if err := qmpReset(socket, bound); err != nil {
		return nil, err
	}
	return awaitStart(r, bound)
}

// OpenCmd opens the board on port and resets it by running argv, the probe
// tool's reset (TFW_HW_RESET=cmd:...), then waits up to bound for its first
// transmission, as Open does after a DTR pulse. DTR is left alone.
func OpenCmd(port string, argv []string, bound time.Duration) (io.ReadWriteCloser, error) {
	// The reset runs through runHook, which bounds it and kills the whole
	// process group, so a hook whose child holds its output cannot hang it.
	hook := strings.Join(argv, " ")
	if addr, ok := strings.CutPrefix(port, TCPListenPrefix); ok {
		return openListen(port, addr, func() error { return runHook(hook, bound) }, bound)
	}
	if addr, ok := strings.CutPrefix(port, TCPPrefix); ok {
		// A board whose wire is TCP (the STM32H573I-DK's ETH_MAC=1 image):
		// the reset goes first, then the dial, which is redialled while the
		// board boots and takes its lease. Hook and dials share one
		// absolute deadline, so the whole sequence ends within the bound.
		deadline := now().Add(bound)
		if err := runHook(hook, bound); err != nil {
			return nil, fmt.Errorf("%s: resetting the board: %w", port, err)
		}
		c, err := openTCPAfterReset(addr, deadline)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", port, err)
		}
		return c, nil
	}
	p, err := openPort(port, 0)
	if err != nil {
		return nil, err
	}
	c, err := startCmd(p, func() error { return runHook(hook, bound) }, bound)
	if err != nil {
		_ = p.Close()
		return nil, fmt.Errorf("%s: %w", port, err)
	}
	return c, nil
}

// tcpRedialPause is the wait between dials of a board still booting after
// its reset command.
var tcpRedialPause = 500 * time.Millisecond

// openTCPAfterReset dials a board that was just reset until it answers or
// the bound passes; the last dial's error is returned.
func openTCPAfterReset(addr string, deadline time.Time) (io.ReadWriteCloser, error) {
	var last error
	within := func() (time.Duration, error) {
		left := deadline.Sub(now())
		if left <= 0 {
			return 0, errTCPResetDeadline(last)
		}
		return left, nil
	}
	for {
		c, err := openTCPWithin(addr, within)
		if err == nil {
			if now().After(deadline) {
				// A connection completed past the deadline is not the
				// board answering within the bound.
				_ = c.Close()
				return nil, errTCPResetDeadline(last)
			}
			return c, nil
		}
		last = err
		left := deadline.Sub(now())
		if left <= tcpRedialPause {
			return nil, err
		}
		sleep(tcpRedialPause)
	}
}

// now and sleep are the clock the TCP reset deadline is kept on; tests
// replace them with a fake clock.
var (
	now   = time.Now
	sleep = time.Sleep
)

// errTCPResetDeadline is the error of a reset board not answering by the
// deadline, carrying the last dial's error when there was one.
func errTCPResetDeadline(last error) error {
	if last == nil {
		return errResetDeadline
	}
	if errors.Is(last, errResetDeadline) {
		return last
	}
	return fmt.Errorf("%w: %w", errResetDeadline, last)
}

// errResetDeadline marks a reset sequence that ran out of its bound.
var errResetDeadline = errors.New("the board did not answer within the bound")

// startCmd purges what the running application sent, then resets, then keeps
// the new application's first bytes: the order startQMP keeps, since the
// command returns only after the reset, by when the BOOT event may be in.
func startCmd(p io.ReadWriteCloser, reset func() error, bound time.Duration) (io.ReadWriteCloser, error) {
	r, ok := p.(purgeable)
	if !ok {
		return nil, errors.New("the port cannot purge its receive buffer")
	}
	if err := r.ResetInputBuffer(); err != nil {
		return nil, fmt.Errorf("purging the receive buffer: %w", err)
	}
	if err := reset(); err != nil {
		return nil, fmt.Errorf("resetting the board: %w", err)
	}
	return awaitStart(r, bound)
}

// qmpMessage is one QMP server message: a command's return or error, or an
// asynchronous event (QEMU's docs/interop/qmp-spec.rst).
type qmpMessage struct {
	Return json.RawMessage `json:"return"`
	Error  *struct {
		Class string `json:"class"`
		Desc  string `json:"desc"`
	} `json:"error"`
	Event string `json:"event"`
}

// qmpReset resets the guest of the QEMU whose QMP monitor listens on socket:
// it reads the greeting, leaves capabilities negotiation with
// qmp_capabilities, sends system_reset and waits for the RESET event that
// says the reset happened -- system_reset only requests one. The whole
// exchange is bounded by bound.
func qmpReset(socket string, bound time.Duration) error {
	conn, err := net.DialTimeout("unix", socket, bound)
	if err != nil {
		return fmt.Errorf("%s=qmp:%s: %w", ResetEnv, socket, err)
	}
	defer func() { _ = conn.Close() }()
	if err := conn.SetDeadline(time.Now().Add(bound)); err != nil {
		return fmt.Errorf("qmp: %w", err)
	}
	dec := json.NewDecoder(conn)
	var greeting struct {
		QMP json.RawMessage `json:"QMP"`
	}
	if err := dec.Decode(&greeting); err != nil {
		return fmt.Errorf("qmp: reading the greeting: %w", err)
	}
	if greeting.QMP == nil {
		return fmt.Errorf("%s=qmp:%s: the socket's first message is not a QMP greeting", ResetEnv, socket)
	}
	reset := false
	for _, cmd := range []string{"qmp_capabilities", "system_reset"} {
		if _, err := fmt.Fprintf(conn, "{\"execute\":%q}\n", cmd); err != nil {
			return fmt.Errorf("qmp %s: %w", cmd, err)
		}
		for answered := false; !answered; {
			var m qmpMessage
			if err := dec.Decode(&m); err != nil {
				return fmt.Errorf("qmp %s: %w", cmd, err)
			}
			switch {
			case m.Error != nil:
				return fmt.Errorf("qmp %s: %s: %s", cmd, m.Error.Class, m.Error.Desc)
			case m.Return != nil:
				answered = true
			case m.Event == "RESET" && cmd == "system_reset":
				reset = true // QEMU may emit it before the return
			}
		}
	}
	for !reset {
		var m qmpMessage
		if err := dec.Decode(&m); err != nil {
			return fmt.Errorf("qmp: waiting for the RESET event: %w", err)
		}
		reset = m.Event == "RESET"
	}
	return nil
}

// ResetPulse is how long DTR is held off before it is asserted again.
const ResetPulse = 100 * time.Millisecond

// resettable is what start needs of a serial port beyond reading and writing.
type resettable interface {
	io.ReadWriteCloser
	SetDTR(bool) error
	ResetInputBuffer() error
}

// start resets the board through DTR and waits for it to transmit.
//
// Opening the port is not relied on to reset the board, though it usually
// does: DTR rising is what resets an Arduino (its USB bridge couples DTR to
// RESET through a capacitor), and the kernel raises DTR on open only if it
// dropped it on the last close. A port left with -hupcl, as a terminal
// session or a probe run leaves it, keeps DTR up, so the board would not
// reset, would send no BOOT event, and each test would inherit the last one's
// state. Each test gets a freshly reset board instead, as it gets a fresh
// tfwsim process. This is the reset avrdude performs before it talks to the
// bootloader.
func start(p io.ReadWriteCloser, bound time.Duration) (io.ReadWriteCloser, error) {
	r, ok := p.(resettable)
	if !ok {
		return nil, errors.New("the port cannot drive DTR, so the board cannot be reset")
	}
	if err := r.SetDTR(false); err != nil {
		return nil, fmt.Errorf("dropping DTR: %w", err)
	}
	time.Sleep(ResetPulse)
	if err := r.SetDTR(true); err != nil {
		return nil, fmt.Errorf("raising DTR: %w", err)
	}
	// Whatever the old application sent before the reset; the bootloader
	// runs for about a second, so this is long before the new one starts.
	if err := r.ResetInputBuffer(); err != nil {
		return nil, fmt.Errorf("purging the receive buffer: %w", err)
	}
	return awaitStart(r, bound)
}

// ModemLines is what ESPReset needs of the reset port.
type ModemLines interface {
	SetDTR(bool) error
	SetRTS(bool) error
}

// ESPReset is esptool's classic reset on the ESP32 auto-reset circuit
// (#550): RTS drives EN and DTR drives GPIO0, each asserted
// line pulling its pin low, and both asserted together pull neither. DTR is
// released first so GPIO0 is high when EN rises (a normal boot, not the ROM
// download mode); RTS then holds EN low for ResetPulse and releases it.
// Pulsing DTR alone never moves EN, so it does not reset a running board.
func ESPReset(r ModemLines) error {
	if err := r.SetDTR(false); err != nil {
		return fmt.Errorf("releasing DTR (GPIO0 high): %w", err)
	}
	if err := r.SetRTS(true); err != nil {
		return fmt.Errorf("asserting RTS (EN low): %w", err)
	}
	time.Sleep(ResetPulse)
	if err := r.SetRTS(false); err != nil {
		return fmt.Errorf("releasing RTS (EN high): %w", err)
	}
	return nil
}

// awaitStart reads until rwc yields its first bytes or bound passes. rwc must
// return from Read within a bounded time even when nothing arrives, as
// serialport.Open's read timeout makes it.
func awaitStart(rwc io.ReadWriteCloser, bound time.Duration) (io.ReadWriteCloser, error) {
	deadline := time.Now().Add(bound)
	buf := make([]byte, 256)
	for {
		n, err := rwc.Read(buf)
		if n > 0 {
			return &started{rwc: rwc, pending: buf[:n]}, nil
		}
		if err != nil {
			return nil, fmt.Errorf("waiting for the board to start: %w", err)
		}
		if time.Now().After(deadline) {
			return nil, fmt.Errorf("%w (waited %s)", ErrSilent, bound)
		}
	}
}

// started is a board's port with the bytes awaitStart read put back in front.
// It deliberately offers no ResetInputBuffer: tfw.Device purges a conn that
// does before it waits for BOOT, and the BOOT event is what is pending here.
type started struct {
	rwc     io.ReadWriteCloser
	pending []byte
}

func (s *started) Read(p []byte) (int, error) {
	if len(s.pending) > 0 {
		n := copy(p, s.pending)
		s.pending = s.pending[n:]
		return n, nil
	}
	return s.rwc.Read(p)
}

func (s *started) Write(p []byte) (int, error) { return s.rwc.Write(p) }
func (s *started) Close() error                { return s.rwc.Close() }

// SetReadDeadline passes the Device's read bound through to the conn, so a
// board that sends its BOOT and then stalls fails its read by the deadline
// rather than at the test's timeout. A conn with no deadlines keeps none.
func (s *started) SetReadDeadline(t time.Time) error {
	if c, ok := s.rwc.(interface{ SetReadDeadline(time.Time) error }); ok {
		return c.SetReadDeadline(t)
	}
	return nil
}

// SetDeadline passes a read and write bound through to the conn.
func (s *started) SetDeadline(t time.Time) error {
	if c, ok := s.rwc.(interface{ SetDeadline(time.Time) error }); ok {
		return c.SetDeadline(t)
	}
	return s.SetReadDeadline(t)
}
