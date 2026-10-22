// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package peer is the one seam between the SDK's cross-implementation suite
// and the firmware it talks to.
//
// By default the peer is tfwsim, the firmware compiled for the host, launched
// through simproc. When PortEnv names a serial port the peer is a real board on
// that port instead (`make hw-test`), and BoardEnv says which board it is. The
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
	"errors"
	"fmt"
	"io"
	"net/netip"
	"os"
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

// ErrSilent reports a board that transmitted nothing within the bound.
var ErrSilent = errors.New("the board sent nothing after its port opened -- is the firmware flashed, and is this its port?")

// Target is the peer the suite talks to.
type Target struct {
	Port      string // empty for the simulator
	Board     string
	Eth       string // where the board dials; empty for the simulator and a serial-only run
	EthListen string // where the suite listens for it, when not Eth; empty for Eth
}

// Hardware reports whether the target is a real board.
func (t Target) Hardware() bool { return t.Port != "" }

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
	switch {
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
	return Target{Port: port, Board: board, Eth: eth, EthListen: listen}, nil
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
	c, err := Open(t.Port, StartBound)
	if err != nil {
		tb.Fatal(err)
	}
	tb.Cleanup(func() { _ = c.Close() })
	return c
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
