// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/serialport"
)

// The TestFirmware* scenarios run each verb against whatever the transport
// seam, internal/peer, names: tfwsim -- the firmware compiled for the host --
// when no port is set, and the board on TFW_HW_PORT under `make hw-test`. So
// one scenario is the cross-implementation check in CI and the bench check on a
// board, and none of them picks a transport for itself.

// The pin facts of each board, peerBoards, are in e2e_pins_test.go, which
// every build compiles, so a unit test checks them against the row lists.

// firmwareBoard returns the facts for the board the seam names. A board this
// table does not know fails the scenario: guessing its pins would test the
// guess.
func firmwareBoard(t *testing.T) (string, peerBoard) {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	b, ok := peerBoards[target.Board]
	require.True(t, ok, "%s=%s: no pin facts for that board in peerBoards", peer.BoardEnv, target.Board)
	return target.Board, b
}

// The ESP32-P4's header has no LED and its tick runs on a timer no pin
// shares (boards/esp32p4-poe/board.c): its facts must say both, or the pin
// and PWM rows demand an LED flag and a refused duty the board cannot give.
func TestPeerBoardFactsSayWhatTheBoardLacks(t *testing.T) {
	p4 := peerBoards["esp32p4-poe"]
	assert.True(t, p4.noLED, "esp32p4-poe has no LED; ledPin is a plain digital pin")
	assert.Negative(t, p4.tickPin, "esp32p4-poe's tick timer drives no pin")
	assert.EqualValues(t, 0, p4.pwmPin, "esp32p4-poe's free PWM pin is GPIO2, LEDC0, not the AVRs' pin 9 (an ADC pin there)")
	for _, name := range []string{"atmega328p", "atmega2560"} {
		b := peerBoards[name]
		assert.False(t, b.noLED, "%s has an LED on pin 13", name)
		assert.GreaterOrEqual(t, b.tickPin, 0, "%s ticks on timer0, whose compare pins are spent", name)
	}
}

// startFirmware opens the device the seam names. A missing simulator binary or
// a port that does not answer fails the scenario; neither is a skip.
func startFirmware(t *testing.T, opts ...tfw.Option) *tfw.Device {
	t.Helper()
	// WithTimeout first so a caller's own option, if any, wins.
	base := []tfw.Option{tfw.WithTimeout(5 * time.Second)}
	if !firmwareResets(t) {
		// No reset, so no BOOT to wait for (peer.Target.Resets).
		base = append(base, tfw.WithBootWait(0))
	}
	return tfw.New(peer.Start(t, simproc.Software), append(base, opts...)...)
}

// cliTier is the crypto tier the device's caps verb names.
func cliTier(t *testing.T, dev *tfw.Device) string {
	t.Helper()
	return cliCaps(t, dev).CryptoTier.String()
}

// cliRefused runs a verb the device must refuse, with no further claim on which
// status: the original rows asked only that the device say no.
func cliRefused(t *testing.T, dev *tfw.Device, verb string, args ...string) {
	t.Helper()
	_ = cliStatus(t, dev, verb, args...)
}

func cliPin(t *testing.T, dev *tfw.Device, pin uint8) tfw.PinInfo {
	t.Helper()
	var p tfw.PinInfo
	cliJSON(t, dev, &p, "pin", strconv.Itoa(int(pin)))
	return p
}

type pingJSON struct {
	Echo string `json:"echo"`
}

func TestFirmwarePing(t *testing.T) {
	dev := startFirmware(t) // the default boot wait: tfwsim really does emit TFW_EVENT_BOOT
	var got pingJSON
	cliJSON(t, dev, &got, "ping", "pong")
	assert.Equal(t, "pong", got.Echo)
	// With the boot wait off (a tcp: link that does not reset the board), the
	// first host after boot still reads the BOOT the board buffered for it
	// (hal_uart.c holds startup output until a host connects); a later host
	// reads none. Consume that one BOOT, never wait for it.
	if !firmwareResets(t) {
		assert.Empty(t, dropBufferedBoot(dev.Events()), "only a buffered BOOT may precede the first reply")
	}

	full := strings.Repeat("m", tfw.MaxPayload)
	cliJSON(t, dev, &got, "ping", full)
	assert.Equal(t, full, got.Echo, "ping of the maximum payload")

	// A sustained exchange: two hundred round trips on one link, each echo its own.
	for i := 0; i < 200; i++ {
		want := strconv.Itoa(i)
		cliJSON(t, dev, &got, "ping", want)
		require.Equal(t, want, got.Echo, "exchange %d", i)
	}
	// The startup BOOT event is consumed by the boot wait, not reported.
	assert.Empty(t, dev.Events(), "the startup BOOT event must be consumed, not reported")
}

// dropBufferedBoot returns events without a leading BOOT event: at most one,
// the one a board buffered before its first host connected.
func dropBufferedBoot(events []tfw.Frame) []tfw.Frame {
	if len(events) > 0 && events[0].IsEvent() && len(events[0].Payload) > 0 && events[0].Payload[0] == tfw.EventBoot {
		return events[1:]
	}
	return events
}

// espWirePort serves a fresh tfwsim behind serveESPWire, as the ESP32-P4's
// tcp: wire, and points the rows at it. The sim's BOOT is held for the first host.
func espWirePort(t *testing.T) string {
	t.Helper()
	sim := simproc.Start(t, simproc.Software)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	t.Cleanup(func() { _ = ln.Close() })
	serveESPWire(ln, sim, func() {})
	port := peer.TCPPrefix + ln.Addr().String()
	t.Setenv(peer.PortEnv, port)
	t.Setenv(peer.BoardEnv, "esp32p4-poe")
	return port
}

func TestPingRowOnTheESP32OverTCPAsTheFirstHostAfterBoot(t *testing.T) {
	espWirePort(t)
	require.True(t, t.Run("TestFirmwarePing", TestFirmwarePing), "the Ping row failed as the first host after boot")
}

func TestPingRowOnTheESP32OverTCPAsALaterHost(t *testing.T) {
	port := espWirePort(t)
	// A first host takes the buffered BOOT and leaves.
	c, err := peer.Open(port, peer.StartBound)
	require.NoError(t, err)
	first := tfw.New(c, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0))
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	_, err = first.Ping(ctx, []byte("x"))
	cancel()
	events := first.Events()
	_ = c.Close()
	require.NoError(t, err)
	require.NotEmpty(t, events, "the first host saw no BOOT, so the later host proves nothing")
	require.Len(t, dropBufferedBoot(events), len(events)-1, "the first host's first event is not BOOT")

	require.True(t, t.Run("TestFirmwarePing", TestFirmwarePing), "the Ping row failed as a later host")
}

func TestFirmwarePingDeliversTheBootEventWithNoBootWait(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	conn, skip := bootEventLink(t, target, os.Getenv(resetPortEnv))
	if skip != "" {
		t.Skip(skip)
	}
	assertBootEvent(t, conn)
}

// assertBootEvent pings over conn, a link to a board that has just booted,
// with no boot wait and asserts the first event the device delivered is its
// boot event.
func assertBootEvent(t *testing.T, conn io.ReadWriteCloser) {
	t.Helper()
	bootEventRow(t, tfw.New(conn, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0)), true)
}

// firmwareResets reports whether the run's target is reset on connect, so it
// sends BOOT and starts with clean counters (peer.Target.Resets). A board on
// a TCP wire is found running, with the state earlier rows left on it.
func firmwareResets(t *testing.T) bool {
	t.Helper()
	target, err := peer.FromEnv()
	require.NoError(t, err)
	return target.Resets()
}

// bootEventRow: a reset peer's first event is BOOT. A peer found running
// sends none on connect: connecting does not restart it.
func bootEventRow(t *testing.T, dev *tfw.Device, resets bool) {
	t.Helper()
	var got pingJSON
	cliJSON(t, dev, &got, "ping")
	events := dev.Events()
	if !resets {
		for _, e := range events {
			assert.False(t, e.IsEvent() && len(e.Payload) > 0 && e.Payload[0] == tfw.EventBoot,
				"a board found running sent BOOT on connect: %+v", e)
		}
		return
	}
	require.NotEmpty(t, events, "no boot event was observed")
	assert.True(t, events[0].IsEvent() && events[0].Payload[0] == tfw.EventBoot,
		"first event = %+v, want a boot event", events[0])
}

// resetPortEnv names the serial port that resets a board whose wire is a
// tcp: link (#550, make hw-test RESET_PORT=): the suite pulses its DTR and
// nothing else, and talks to the board over TCP.
const resetPortEnv = "TFW_HW_RESET_PORT"

// pulseReset drops DTR on port for peer.ResetPulse and raises it again,
// resetting the board; a variable so a host test can see the pulse.
var pulseReset = func(port string) error {
	p, err := serialport.Open(port, 0)
	if err != nil {
		return err
	}
	defer func() { _ = p.Close() }()
	r, ok := p.(peer.ModemLines)
	if !ok {
		return fmt.Errorf("%s cannot drive RTS and DTR", port)
	}
	if err := peer.ESPReset(r); err != nil {
		return fmt.Errorf("%s: %w", port, err)
	}
	return nil
}

// bootEventLink is the link the boot-event row reads, to a board that has
// just booted. A serial or QMP link is the seam's, which resets the board as
// it opens it. A tcp: link has no reset of its own: the board is reset by a
// pulse on resetPort and its endpoint dialled again until it is back. With no
// resetPort nothing resets it, so there is no link and skip says why: the row
// skips by name, never passes on a boot it did not see.
func bootEventLink(t *testing.T, target peer.Target, resetPort string) (conn io.ReadWriteCloser, skip string) {
	t.Helper()
	if !strings.HasPrefix(target.Port, peer.TCPPrefix) {
		return peer.Start(t, simproc.Software), ""
	}
	if resetPort == "" {
		return nil, fmt.Sprintf("%s=%s is a tcp: link and %s is unset: nothing resets the board, so its boot event cannot be observed",
			peer.PortEnv, target.Port, resetPortEnv)
	}
	require.NoError(t, pulseReset(resetPort), "%s=%s", resetPortEnv, resetPort)
	// The board answers once it is back; poll for that, not for a fixed time.
	var c io.ReadWriteCloser
	require.EventuallyWithT(t, func(ct *assert.CollectT) {
		var err error
		c, err = peer.Open(target.Port, peer.StartBound)
		assert.NoError(ct, err, "%s did not answer within %v of the reset", target.Port, peer.StartBound)
	}, peer.StartBound, peer.ResetPulse)
	t.Cleanup(func() { _ = c.Close() })
	return c, ""
}

// TestBootEventLinkSkipsATCPLinkWithNoResetPort: with no reset port the
// boot-event row has no link and a reason naming the variable to set.
func TestBootEventLinkSkipsATCPLinkWithNoResetPort(t *testing.T) {
	conn, skip := bootEventLink(t, peer.Target{Port: peer.TCPPrefix + "192.0.2.1:7000"}, "")
	assert.Nil(t, conn)
	assert.Contains(t, skip, resetPortEnv)
	assert.Contains(t, skip, peer.TCPPrefix+"192.0.2.1:7000")
}

// TestBootEventLinkResetsATCPLinkThroughTheResetPort runs the boot-event row
// on a simulated tcp: peer, a listener bridged to the software simulator with
// the ESP32 wire's accept/drop ordering (serveESPWire), so the boot event is
// sent before any host is accepted, as on the board: the
// reset port is pulsed before the endpoint is dialled, and the boot event
// arrives over TCP.
func TestBootEventLinkResetsATCPLinkThroughTheResetPort(t *testing.T) {
	sim := simproc.Start(t, simproc.Software)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	t.Cleanup(func() { _ = ln.Close() })
	var pulsed, dialledAfterPulse atomic.Bool
	serveESPWire(ln, sim, func() { dialledAfterPulse.Store(pulsed.Load()) })
	restore := pulseReset
	t.Cleanup(func() { pulseReset = restore })
	var pulsedPort string
	pulseReset = func(port string) error {
		pulsedPort = port
		pulsed.Store(true)
		return nil
	}

	conn, skip := bootEventLink(t, peer.Target{Port: peer.TCPPrefix + ln.Addr().String()}, "reset-port-under-test")
	require.Empty(t, skip)
	assert.Equal(t, "reset-port-under-test", pulsedPort)
	assertBootEvent(t, conn)
	assert.True(t, dialledAfterPulse.Load(), "the endpoint was dialled before the reset")
}

// linePort records the modem-line changes peer.ESPReset makes, and when.
type linePort struct {
	calls []string
	at    []time.Time
	fail  string // the call that fails, if any
}

func (l *linePort) set(line string, on bool) error {
	call := fmt.Sprintf("%s=%t", line, on)
	l.calls, l.at = append(l.calls, call), append(l.at, time.Now())
	if call == l.fail {
		return errors.New("line stuck")
	}
	return nil
}

func (l *linePort) SetDTR(on bool) error { return l.set("DTR", on) }
func (l *linePort) SetRTS(on bool) error { return l.set("RTS", on) }

// The reset pulses EN through RTS with GPIO0 (DTR) released, so a running
// ESP32 reboots into its application and sends BOOT again.
func TestESPResetPulsesENThroughRTSWithDTRReleased(t *testing.T) {
	l := &linePort{}
	require.NoError(t, peer.ESPReset(l))
	require.Equal(t, []string{"DTR=false", "RTS=true", "RTS=false"}, l.calls)
	assert.GreaterOrEqual(t, l.at[2].Sub(l.at[1]), peer.ResetPulse, "EN held low too briefly")
}

// A line that will not move is an error, and the sequence stops there.
func TestESPResetReportsALineThatFails(t *testing.T) {
	for _, fail := range []string{"DTR=false", "RTS=true", "RTS=false"} {
		l := &linePort{fail: fail}
		err := peer.ESPReset(l)
		require.Error(t, err, fail)
		assert.Equal(t, fail, l.calls[len(l.calls)-1], "a call after the failed one")
	}
}

// espWireHold is TX_BUF in src/hal/esp32/hal_uart.c.
const espWireHold = 512

// serveESPWire bridges ln to board with the ESP32 wire's ordering
// (src/hal/esp32/hal_uart.c): the board runs from boot whether a host is
// connected or not; what it sends before the first host is accepted is held,
// up to espWireHold bytes (all of it or, on overflow, none), and goes to that
// host on accept; what it sends with no host after that is dropped.
func serveESPWire(ln net.Listener, board io.ReadWriter, onAccept func()) {
	var (
		mu       sync.Mutex
		client   net.Conn
		accepted bool
		holding  = true
		held     []byte
		// booted closes once the reader has taken the board's first output
		// (the boot event): the accept loop waits on it, so the first host is
		// always accepted after that output, as on the device, never racing it.
		booted   = make(chan struct{})
		bootOnce sync.Once
	)
	go func() {
		buf := make([]byte, 256)
		defer bootOnce.Do(func() { close(booted) })
		for {
			n, err := board.Read(buf)
			mu.Lock()
			switch {
			case client != nil:
				_, _ = client.Write(buf[:n])
			case !accepted && holding && len(held)+n <= espWireHold:
				held = append(held, buf[:n]...)
			case !accepted:
				holding, held = false, nil
			}
			mu.Unlock()
			if n > 0 {
				bootOnce.Do(func() { close(booted) })
			}
			if err != nil {
				return
			}
		}
	}()
	go func() {
		<-booted
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			onAccept()
			mu.Lock()
			if client != nil {
				mu.Unlock()
				_ = c.Close()
				continue
			}
			client, accepted = c, true
			_, _ = c.Write(held)
			held = nil
			mu.Unlock()
			go func() {
				_, _ = io.Copy(board, c)
				mu.Lock()
				if client == c {
					client = nil
				}
				mu.Unlock()
			}()
		}
	}()
}

// Forty pairs of (SOF, SOF). The framer treats the first byte of each pair as a
// candidate SOF and the second as its LEN -- itself SOF's own value, an
// implausible length -- so RESYNC (frame.h) reprocesses that same byte as the
// NEXT pair's candidate SOF. Forty BAD_FORMAT events from eighty bytes of pure
// noise, well past the four-times-smaller bound pump() used to have, and the
// ping after them is still answered.
func TestFirmwarePingAfterAMalformedBurst(t *testing.T) {
	conn := peer.Start(t, simproc.Software)
	noise := make([]byte, 0, 80)
	for i := 0; i < 40; i++ {
		noise = append(noise, tfw.SOF, tfw.SOF)
	}
	_, err := conn.Write(noise)
	require.NoError(t, err)
	dev := tfw.New(conn, tfw.WithTimeout(5*time.Second))
	var got pingJSON
	cliJSON(t, dev, &got, "ping", "hi")
	assert.Equal(t, "hi", got.Echo)
}

func TestFirmwareCaps(t *testing.T) {
	name, _ := firmwareBoard(t)
	dev := startFirmware(t)
	c := cliCaps(t, dev)
	// The pin facts these scenarios assert are the named board's; a peer that
	// is some other board would fail them for the wrong reason.
	assert.Equal(t, name, c.BoardID.String(), "board")
	assert.EqualValues(t, 2, c.ProtoMajor, "protocol major")
	assert.EqualValues(t, tfw.MaxPayload, c.MaxPayload,
		"device MaxPayload and the host constant must agree or a host will send frames the device refuses")
	assert.True(t, c.Has(tfw.FeatureGPIO), "GPIO not reported; the default build should have it")
	// CAPS must report the crypto tier it actually has, so a verifier learns
	// what it is talking to before it decides what the evidence is worth
	// (docs/wire-protocol/api.md:63), and what it can sign with:
	// docs/dice-attestation/user.md, a SOFTWARE-tier device signs ES256 under
	// its P-256 DeviceID (SOFT_ID=p256: tfwsim and the ATmega2560), except the
	// ATmega328P and ATmega32U4 images, which keep the HMAC-SHA256 signer
	// (SOFT_ID=hmac); docs/dice-attestation/cli.md, the element performs
	// ECDSA-P256-SHA256 and HMAC-SHA256. Neither performs Ed25519. A CRYPTO=none
	// image (the ESP32-P4 PoE at HIL-0) has no provider and reports no
	// algorithm at all (src/crypto/none/provider.c, tfw_crypto_algorithms).
	target, err := peer.FromEnv()
	require.NoError(t, err)
	soft := tfw.AlgECDSAP256SHA256
	if target.Hardware() && (target.Board == "atmega328p" || target.Board == "atmega32u4") {
		soft = tfw.AlgHMACSHA256
	}
	capsTierRow(t, c, soft)
}

// capsTierRow is TestFirmwareCaps's tier facts, so a host test can run them
// against a peer of any tier; soft is the software tier's signer. The none
// provider's tfw_crypto_algorithms answers 0 (src/crypto/none/provider.c):
// a host negotiating against it finds nothing to select. ATTEST stays in
// CAPS on every tier; the none tier answers it UNSUPPORTED
// (attestRefusedUnprovisionedRow).
func capsTierRow(t *testing.T, c tfw.Caps, soft tfw.AlgorithmSet) {
	t.Helper()
	assert.True(t, c.Has(tfw.FeatureAttest), "attestation compiled in but not reported in CAPS")
	switch tier := c.CryptoTier.String(); tier {
	case "software":
		assert.Equal(t, soft, c.Algorithms, "algorithms = %s, want %s alone", c.Algorithms, soft)
	case "secure-element":
		assert.Equal(t, tfw.AlgHMACSHA256|tfw.AlgECDSAP256SHA256, c.Algorithms,
			"algorithms = %s, want ecdsa-p256-sha256 and hmac-sha256", c.Algorithms)
	case "none":
		assert.Zero(t, c.Algorithms, "algorithms = %s, want none on a CRYPTO=none image", c.Algorithms)
	default:
		t.Fatalf("crypto tier %s: no facts for it", tier)
	}
	assert.False(t, c.Algorithms.Can(tfw.AlgEd25519), "the provider claimed Ed25519, which no tier can perform")
}

func TestFirmwareSys(t *testing.T) {
	_, b := firmwareBoard(t)
	resets := firmwareResets(t)
	sysRow(t, startFirmware(t), b.causes(resets), resets)
}

// sysRow: SYS_INFO decodes against the live device. #5: a reset peer has
// seen no framing errors; a peer found running keeps the counters earlier
// rows raised (a malformed burst), so a clean exchange must add none. Its
// reset cause is its last boot's, one of causes, and the same after the
// exchange: a cause that changes means the board restarted under the row.
func sysRow(t testing.TB, dev *tfw.Device, causes []tfw.ResetCause, resets bool) {
	t.Helper()
	var si tfw.SysInfo
	cliJSON(t, dev, &si, "sys")
	assert.NotZero(t, si.FreeRAM, "free RAM reported as zero")
	assert.Contains(t, causes, si.ResetCause, "reset cause")
	if resets {
		assert.Zero(t, si.ErrCRC, "framer CRC errors on a clean link")
		assert.Zero(t, si.ErrFormat, "framer format errors on a clean link")
		return
	}
	// The counters are sticky: an image whose connect does not reset it (a
	// TCP peer) still holds what an earlier row, such as
	// TestFirmwarePingAfterAMalformedBurst, put there (Codex P1 on #565), so
	// clean traffic must leave them where they were.
	var pong pingJSON
	for i := 0; i < 20; i++ {
		cliJSON(t, dev, &pong, "ping", strconv.Itoa(i))
	}
	var after tfw.SysInfo
	cliJSON(t, dev, &after, "sys")
	assert.Equal(t, si.ErrCRC, after.ErrCRC, "framer CRC errors grew over a clean link")
	assert.Equal(t, si.ErrFormat, after.ErrFormat, "framer format errors grew over a clean link")
	assert.Equal(t, si.ResetCause, after.ResetCause, "reset cause changed across exchanges: the board restarted")
}

// causes are the reset causes the SYS row accepts: resetCause on a board the
// run resets, heldCauses on one it finds running.
func (b peerBoard) causes(resets bool) []tfw.ResetCause {
	if resets || b.heldCauses == nil {
		return []tfw.ResetCause{b.resetCause}
	}
	return b.heldCauses
}

// sysCountersHold runs a ping over dev and asserts the framer's CRC and
// format counters still read as they did in before.
func sysCountersHold(t *testing.T, dev *tfw.Device, before tfw.SysInfo) {
	t.Helper()
	var got pingJSON
	cliJSON(t, dev, &got, "ping", "hi")
	var after tfw.SysInfo
	cliJSON(t, dev, &after, "sys")
	assert.Equal(t, before.ErrCRC, after.ErrCRC, "framer CRC errors on a clean link")
	assert.Equal(t, before.ErrFormat, after.ErrFormat, "framer format errors on a clean link")
}

// TestSysCountersHoldAfterAMalformedBurst is the Sys row on a board an
// earlier row left with raised counters (#550): the burst counts, and the
// row still passes.
func TestSysCountersHoldAfterAMalformedBurst(t *testing.T) {
	conn := simproc.Start(t, simproc.Software)
	noise := make([]byte, 0, 80)
	for i := 0; i < 40; i++ {
		noise = append(noise, tfw.SOF, tfw.SOF)
	}
	_, err := conn.Write(noise)
	require.NoError(t, err)
	dev := tfw.New(conn, tfw.WithTimeout(5*time.Second))
	var si tfw.SysInfo
	cliJSON(t, dev, &si, "sys")
	require.NotZero(t, si.ErrCRC+si.ErrFormat, "the burst raised no counter, so this proves nothing")
	sysCountersHold(t, dev, si)
}

// The Sys row itself, as hw-test runs it on the ESP32-P4 (#550): BOARD
// esp32p4-poe, PORT tcp: on a simulated tcp peer whose firmware took a
// malformed burst first and was never reset. A first host sees the burst
// counted; the row, the host after it, passes with the counters raised.
func TestSysRowOnTheESP32OverTCPAfterAMalformedBurst(t *testing.T) {
	sim := simproc.Start(t, simproc.Software)
	noise := make([]byte, 0, 80)
	for i := 0; i < 40; i++ {
		noise = append(noise, tfw.SOF, tfw.SOF)
	}
	_, err := sim.Write(noise)
	require.NoError(t, err)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	require.NoError(t, err)
	t.Cleanup(func() { _ = ln.Close() })
	serveESPWire(ln, sim, func() {})
	port := peer.TCPPrefix + ln.Addr().String()
	t.Setenv(peer.PortEnv, port)
	t.Setenv(peer.BoardEnv, "esp32p4-poe")

	// A first host sees the burst counted, then leaves: the row is the next
	// host, so it also reconnects as a row after another one does.
	c, err := peer.Open(port, peer.StartBound)
	require.NoError(t, err)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	si, err := tfw.New(c, tfw.WithTimeout(5*time.Second)).SysInfo(ctx)
	cancel()
	_ = c.Close()
	require.NoError(t, err)
	require.NotZero(t, si.ErrCRC+si.ErrFormat, "the burst raised no counter, so the row would prove nothing")

	require.True(t, t.Run("TestFirmwareSys", TestFirmwareSys), "the Sys row failed on the ESP32 over tcp:")
}

func TestFirmwarePin(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	caps := cliCaps(t, dev)

	led := cliPin(t, dev, b.ledPin)
	assert.EqualValues(t, b.ledPin, led.Pin, "descriptor echoed the pin")
	want, absent := ledPinFlags(b)
	assert.True(t, led.Flags.Has(want), "pin %d flags = %s, want %s", b.ledPin, led.Flags, want)
	if absent != 0 {
		assert.False(t, led.Flags.Has(absent), "pin %d flags = %s: the board has no LED", b.ledPin, led.Flags)
	}
	assert.True(t, led.HasPort, "pin %d has a port", b.ledPin)
	assert.Equal(t, b.ledPort, led.Port, "pin %d port", b.ledPin)
	assert.Equal(t, b.ledBit, led.Bit, "pin %d bit", b.ledPin)
	assert.False(t, led.HasADC, "pin %d has no analog channel", b.ledPin)
	// None on the 328P, which has no compare unit there; on the Mega it is
	// OC0A, spent on the tick.
	assert.Equal(t, b.ledPWM, led.PWM, "pin %d pwm", b.ledPin)

	// The board's free PWM pin: pin 9 is OC1A on the 328P and OC2B on the
	// Mega, neither timer the tick; LEDC0 on the ESP32-P4; TIM5_CH1 on the
	// STM32H573I-DK.
	if caps.Has(tfw.FeaturePWM) {
		pwm := cliPin(t, dev, b.pwmPin)
		assert.Equal(t, tfw.PWMAvailable, pwm.PWM, "pin %d pwm", b.pwmPin)
		assert.True(t, pwm.Flags.Has(tfw.PinPWM), "pin %d flags = %s, want pwm", b.pwmPin, pwm.Flags)
	}

	// The 328P's A6 exists as an analog input and has no digital port at all,
	// which the descriptor must say rather than report port 0 bit 0 -- a pin
	// that can be read and never written. The Mega's A0 has both.
	if caps.Has(tfw.FeatureAnalog) {
		adc := cliPin(t, dev, b.adcPin)
		assert.Equal(t, b.adcPort, adc.HasPort, "pin %d digital port present", b.adcPin)
		assert.True(t, adc.HasADC, "pin %d has an adc channel", b.adcPin)
		assert.Equal(t, b.adcChannel, adc.ADCChannel, "pin %d adc channel", b.adcPin)
		assert.True(t, adc.Flags.Has(tfw.PinADC), "pin %d flags = %s, want adc", b.adcPin, adc.Flags)
		assert.Equal(t, b.adcPort && !b.adcAnalog, adc.Flags.Has(tfw.PinDigital), "pin %d flags = %s, digital only with a port", b.adcPin, adc.Flags)
	}

	// The UART pins are in use by this build; a host must be able to see that
	// before it drives one and cuts the line it is talking over.
	if b.txPin >= 0 {
		tx := cliPin(t, dev, uint8(b.txPin))
		assert.True(t, tx.Flags.Has(tfw.PinInUse), "pin %d flags = %s, want in-use", b.txPin, tx.Flags)
	}

	assert.Equal(t, tfw.StatusErrBadPin, cliStatus(t, dev, "pin", "200"), "a pin off the end of the board")
}

func TestFirmwareWrite(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	// The transmit line this conversation is happening over.
	if b.txPin >= 0 {
		cliRefused(t, dev, "write", strconv.Itoa(b.txPin), "1")
	}

	// Every pin the device describes as an ADC input with no digital port
	// (the 328P's A6 and A7) must refuse to be driven. The count is the
	// board's, so a board with none, the Mega, proves it has none rather than
	// passing on an empty list.
	found := 0
	for pin := uint8(0); pin < cliCaps(t, dev).PinCount; pin++ {
		info := cliPin(t, dev, pin)
		if !info.HasADC || info.HasPort {
			continue
		}
		found++
		cliRefused(t, dev, "write", strconv.Itoa(int(pin)), "1")
	}
	assert.Equal(t, b.adcOnly, found, "ADC-only pins")
}

// The device, not the host, reverts a held pin: armed, then queried, then left
// to expire with the host doing nothing but asking.
func TestFirmwareTimer(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	var st tfw.TimerState
	pin := strconv.Itoa(int(b.timerPin))

	require.NoError(t, run(context.Background(), dev, "timer", []string{pin, "1", "5s"}, false, &strings.Builder{}))
	var level struct {
		High bool `json:"high"`
	}
	cliJSON(t, dev, &level, "read", pin)
	require.True(t, level.High, "pin did not go high when the timer was armed")
	cliJSON(t, dev, &st, "timer-query", pin)
	require.True(t, st.Active && st.HeldLevel, "timer state = %+v, want active and held high", st)
	require.True(t, st.Remaining > 0 && st.Remaining <= 5*time.Second, "remaining = %s, want 0 < r <= 5s", st.Remaining)

	require.NoError(t, run(context.Background(), dev, "timer", []string{pin, "1", "20ms"}, false, &strings.Builder{}))
	// A wait on the condition, bounded by exchanges: each timer-query is one
	// round trip, and five hundred of them outlast 20 ms on every link.
	const bound = 500
	queries := 0
	for st.Active = true; st.Active && queries < bound; queries++ {
		cliJSON(t, dev, &st, "timer-query", pin)
	}
	require.False(t, st.Active, "wait: the device reverts pin %s's 20ms timer, within %d timer-query exchanges", pin, bound)
	cliJSON(t, dev, &level, "read", pin)
	assert.False(t, level.High, "pin high after the timer expired")
}

// EEPROMReservedEnd grew from 0x80 to 0xC0 when issue #4 added the operator
// public key to the reserved region, and to 0x140 when the confirm request
// took its signature (#125).
func TestFirmwareEEPROM(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureEEPROM)
	cliRefused(t, dev, "eeprom-write", "0", "00000000")   // over the bootloader pubkey hash
	cliRefused(t, dev, "eeprom-write", "192", "00000000") // 0x0C0, the old end: reserved since #125
	// A write starting below the boundary and running through it is refused
	// whole, not partially applied.
	cliRefused(t, dev, "eeprom-write", strconv.Itoa(tfw.EEPROMReservedEnd-1), "0102030405060708")

	// And the user area still works. The bytes it overwrites go back when the
	// test ends, so a run on a board leaves the board on its record (m655).
	var got, old struct {
		Hex string `json:"hex"`
	}
	cliJSON(t, dev, &old, "eeprom-read", "0x180", "2")
	t.Cleanup(func() {
		require.NoError(t, run(context.Background(), dev, "eeprom-write", []string{"0x180", old.Hex}, false, &strings.Builder{}),
			"put back EEPROM 0x180")
		var back struct {
			Hex string `json:"hex"`
		}
		cliJSON(t, dev, &back, "eeprom-read", "0x180", "2")
		assert.Equal(t, old.Hex, back.Hex, "EEPROM 0x180 put back")
	})
	require.NoError(t, run(context.Background(), dev, "eeprom-write", []string{"0x180", "dead"}, false, &strings.Builder{}))
	cliJSON(t, dev, &got, "eeprom-read", "0x180", "2")
	assert.Equal(t, "dead", got.Hex, "read back")

	eepromReservedReadRow(t, dev)
}

// eepromReservedReadRow is TestFirmwareEEPROM's reserved-region read, so a
// host test can run it against a peer of any tier.
// docs/device-provisioning/engineering.md:110: the software tier reads the
// reserved region; the element tier gates the read behind the operator
// session. The none tier links src/core/auth_session_none.c
// (mk/firmware.mk), whose tfw_auth_gate answers OK: it reads it too.
func eepromReservedReadRow(t *testing.T, dev *tfw.Device) {
	t.Helper()
	err := run(context.Background(), dev, "eeprom-read", []string{"0", "4"}, false, &strings.Builder{})
	switch tier := cliTier(t, dev); tier {
	case "software", "none":
		assert.NoError(t, err, "reserved-region read refused on the %s tier", tier)
	case "secure-element":
		assert.Error(t, err, "reserved-region read answered on the element tier without an operator session")
	default:
		t.Fatalf("crypto tier %s: no reserved-read facts for it", tier)
	}
}

func TestFirmwareAttestRefusedUnprovisioned(t *testing.T) {
	// The board's attest timeout, so a boot that signs instead of refusing
	// fails on the status, not on a timeout (#570).
	target, err := peer.FromEnv()
	require.NoError(t, err)
	attestRefusedUnprovisionedRow(t, startFirmware(t, tfw.WithTimeout(attestTimeoutFor(target.Board))))
}

// attestRefusedUnprovisionedRow is the row's body, so a host test can run it
// against a peer of any crypto tier. A tier with no UDS has nothing to sign
// with: the none provider's tfw_crypto_sign answers UNSUPPORTED
// (src/crypto/none/provider.c; docs/boards/esp32p4-poe.md "Building and
// HIL-0"), where a tier that could sign but holds no identity answers
// NOT_PROVISIONED.
func attestRefusedUnprovisionedRow(t *testing.T, dev *tfw.Device) {
	t.Helper()
	nonce := strings.Repeat("00", tfw.AttestNonceLen)
	assert.Equal(t, attestRefusal(cliCaps(t, dev).CryptoTier), cliStatus(t, dev, "attest", nonce),
		"an unprovisioned device produced attestation evidence")
}

func TestFirmwareAnalog(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureAnalog)
	// The mock reports zero until told otherwise; the point is that the
	// command round-trips and the pin maps to a channel at all.
	var got struct {
		Raw uint16 `json:"raw"`
	}
	cliJSON(t, dev, &got, "analog", strconv.Itoa(int(b.analog)))
	// The LED pin is a valid pin with a port and no ADC channel on every
	// board (TestFirmwarePin asserts it), so the refusal is the digital-only
	// one and not an out-of-range pin: 13 is past the DK's 13-pin table.
	cliRefused(t, dev, "analog", strconv.Itoa(int(b.ledPin)))
}

func TestFirmwarePWM(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeaturePWM)
	require.NoError(t, run(context.Background(), dev, "pwm", []string{strconv.Itoa(int(b.pwmPin)), "128"}, false, &strings.Builder{}),
		"pin %d has a free compare unit and should accept a duty", b.pwmPin)
	// Timer0 is the millisecond tick, so its compare pins have units that are
	// spent (5 and 6 on the 328P, 4 and 13 on the Mega). The refusal is what
	// stops a host writing a duty into a timer counting something else and
	// wondering why nothing moves.
	if b.tickPin >= 0 { // the ESP32-P4's and the STM32H573I-DK's ticks spend no compare pin
		cliRefused(t, dev, "pwm", strconv.Itoa(b.tickPin), "128")
	}
	cliRefused(t, dev, "pwm", strconv.Itoa(int(b.noPWMPin)), "128") // no compare unit
}

// toneHz is the frequency the tone row drives. A kilohertz is a 500 us half
// period: long against the P4's busy-polled capture, short enough that the
// edges the row times fit a 200 ms window.
const toneHz = 1000

// TestFirmwareTone starts a tone on the board's own tone pin, measures it
// back on that pin, and stops it. The pin is set to an output first: the P4's
// HAL makes an output pad input-and-output (src/hal/esp32/hal_gpio.c), so
// PULSE_CAPTURE reads the level LEDC drives. On a board the row times the
// captured half periods and wants the frequency within 5%; a simulator's pin
// carries no waveform, so there the row proves the start and stop round-trip.
func TestFirmwareTone(t *testing.T) {
	board, b := firmwareBoard(t)
	target, err := peer.FromEnv()
	require.NoError(t, err)
	// Only an image known to hold the tone pin (the Mega's ETH=1,
	// partTonePinHeld) skips; it runs TestFirmwareToneRefusesTheShieldsPin
	// instead. Anywhere else a RESERVED_PIN on the tone pin fails the row.
	if tonePinHeld(target.Hardware(), board) {
		t.Skipf("%s: the image holds pin %d; TestFirmwareToneRefusesTheShieldsPin covers it", board, b.tonePin)
	}
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureTone)
	pin := strconv.Itoa(int(b.tonePin))

	assert.Equal(t, tfw.StatusErrUnsupported, cliStatus(t, dev, "tone", strconv.Itoa(int(b.pwmPin)), strconv.Itoa(toneHz)),
		"pin %d is not the tone pin", b.pwmPin)
	require.NoError(t, run(context.Background(), dev, "mode", []string{pin, "out"}, false, &strings.Builder{}),
		"%s: pin %d is the tone pin and this image leaves it free", board, b.tonePin)
	require.NoError(t, run(context.Background(), dev, "write", []string{pin, "0"}, false, &strings.Builder{}))
	require.NoError(t, run(context.Background(), dev, "tone", []string{pin, strconv.Itoa(toneHz)}, false, &strings.Builder{}),
		"pin %d is the tone pin and should take %d Hz", b.tonePin, toneHz)
	t.Cleanup(func() {
		_ = run(context.Background(), dev, "notone", []string{pin}, false, &strings.Builder{})
	})

	if target.Hardware() {
		var got struct {
			EdgesUS []uint16 `json:"edges_us"`
		}
		cliJSON(t, dev, &got, "pulse-capture", pin, "1", "30", "50ms")
		// 30 edges: TFW_CAPTURE_MAX is 31 at a 64-byte payload, and TFW_PULSE_MAX_US is 50 ms.
		require.Len(t, got.EdgesUS, 30, "pin %d: a %d Hz tone gives 30 edges in 15 ms", b.tonePin, toneHz)
		hz, err := toneFrequency(got.EdgesUS)
		require.NoError(t, err)
		t.Logf("pin %d: %.1f Hz measured over %d half periods, %d Hz asked", b.tonePin, hz, len(got.EdgesUS)-2, toneHz)
		assert.NoError(t, toneFrequencyFits(board, toneHz, hz), "pin %d: tone frequency", b.tonePin)
	}

	require.NoError(t, run(context.Background(), dev, "notone", []string{pin}, false, &strings.Builder{}))
	if target.Hardware() {
		assert.Equal(t, tfw.StatusErrTimeout, cliStatus(t, dev, "pulse-capture", pin, "1", "4", "20ms"),
			"pin %d: still toggling after NOTONE", b.tonePin)
	}
}

// TestFirmwareToneRefusesTheShieldsPin is the tone row on an image that holds
// the tone pin (partTonePinHeld): the Mega's ETH=1 image, where pin 10 is the
// Ethernet shield's chip select, refuses it as RESERVED_PIN and never drives it.
func TestFirmwareToneRefusesTheShieldsPin(t *testing.T) {
	board, b := firmwareBoard(t)
	target, err := peer.FromEnv()
	require.NoError(t, err)
	// The simulator (a 328P) and the P4 leave the tone pin free, and a run
	// named by HW_TEST_RUN skips the row selector, so the row asks the image.
	if !tonePinHeld(target.Hardware(), board) {
		t.Skipf("%s: the image leaves pin %d free; TestFirmwareTone covers it", board, b.tonePin)
	}
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureTone)
	pin := strconv.Itoa(int(b.tonePin))

	assert.Equal(t, tfw.StatusErrReservedPin, cliStatus(t, dev, "mode", pin, "out"),
		"pin %d is the shield's chip select", b.tonePin)
	// TONE has its own reservation check (src/caps/tone.c): a tone on the
	// chip select would drive the Ethernet chip, so the device refuses it.
	// A tone the device took anyway is stopped before the wire closes.
	err = run(context.Background(), dev, "tone", []string{pin, strconv.Itoa(toneHz)}, false, &strings.Builder{})
	if err == nil {
		t.Cleanup(func() {
			_ = run(context.Background(), dev, "notone", []string{pin}, false, &strings.Builder{})
		})
	}
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "TONE on pin %d: %v, want a device status", b.tonePin, err)
	assert.Equal(t, tfw.StatusErrReservedPin, st, "pin %d is the shield's chip select: TONE must not drive it", b.tonePin)
}

// loopCountCapture names the boards whose pulse-capture reports polling
// iterations, not microseconds: src/hal/avr/hal_pulse.c counts one per
// _delay_us(1) plus the pin reads and arithmetic around it, so each count is
// at least a microsecond and the counts understate the time.
var loopCountCapture = map[string]bool{"atmega328p": true, "atmega2560": true}

// toneFrequencyFits checks a measured tone against the one asked. A board
// timing in microseconds is held to 5% either way; on a loop-count board the
// understated time can only raise the frequency, so only the floor binds.
func toneFrequencyFits(board string, asked, measured float64) error {
	if measured < asked*0.95 {
		return fmt.Errorf("%.1f Hz measured, under 5%% below the %.0f Hz asked", measured, asked)
	}
	if !loopCountCapture[board] && measured > asked*1.05 {
		return fmt.Errorf("%.1f Hz measured, over 5%% above the %.0f Hz asked", measured, asked)
	}
	return nil
}

func TestToneFrequencyFits(t *testing.T) {
	for _, c := range []struct {
		board    string
		measured float64
		fits     bool
	}{
		{"esp32p4-poe", 1000, true},
		{"esp32p4-poe", 1300, false},
		{"esp32p4-poe", 900, false},
		{"atmega2560", 1000, true},
		{"atmega2560", 1300, true}, // loop counts understate the half periods
		{"atmega328p", 900, false},
	} {
		err := toneFrequencyFits(c.board, 1000, c.measured)
		assert.Equal(t, c.fits, err == nil, "%s at %.0f Hz: %v", c.board, c.measured, err)
	}
}

func TestFirmwareI2C(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureI2C)
	assert.Equal(t, tfw.StatusErrProtectedRegion,
		cliStatus(t, dev, "i2c", strconv.Itoa(tfw.I2CReservedAddr), "00", "1"),
		"the secure element's I2C address was reachable")
}

func TestFirmwareI2CConfig(t *testing.T) {
	i2cConfigRow(t, startFirmware(t))
}

// i2cConfigRow is TestFirmwareI2CConfig's body, so a host test can run it
// against a peer of any tier.
func i2cConfigRow(t *testing.T, dev *tfw.Device) {
	t.Helper()
	requireFeature(t, dev, tfw.FeatureI2C)
	ctx := context.Background()
	// src/crypto/atecc608a/atecc.c:61: I2C_CONFIG refuses to touch the TWI
	// peripheral on an element build; the software tier applies the clock, and
	// so does a CRYPTO=none image (src/caps/bus.c refuses on the element only).
	switch tier := cliTier(t, dev); tier {
	case "software", "none":
	case "secure-element":
		cliRefused(t, dev, "i2c-config", "400000")
		return
	default:
		t.Fatalf("crypto tier %s: no I2C_CONFIG facts for it", tier)
	}
	require.NoError(t, run(ctx, dev, "i2c-config", []string{"400000"}, false, &strings.Builder{}),
		"a clock within range should be accepted")
	var ae *tfw.ArgRangeError
	require.ErrorAs(t, run(ctx, dev, "i2c-config", []string{"0"}, false, &strings.Builder{}), &ae, "a zero clock")
}

func TestFirmwareSPIConfig(t *testing.T) {
	dev := startFirmware(t)
	ctx := context.Background()
	mode3, div16 := fmt.Sprint(uint8(tfw.SPIMode3)), fmt.Sprint(uint8(tfw.SPIClockDiv16))
	requireFeature(t, dev, tfw.FeatureSPI)
	require.NoError(t, run(ctx, dev, "spi-config", []string{mode3, div16, "lsb"}, false, &strings.Builder{}),
		"a valid configuration should be accepted")
	var ae *tfw.ArgRangeError
	require.ErrorAs(t, run(ctx, dev, "spi-config", []string{"4", fmt.Sprint(uint8(tfw.SPIClockDiv4)), "msb"}, false, &strings.Builder{}),
		&ae, "a mode past 3")
}

func TestFirmwareOneWire(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureOneWire)
	var reset struct {
		Present bool `json:"present"`
	}
	cliJSON(t, dev, &reset, "1wire-reset", "9")
}

func TestFirmwarePulseIn(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeaturePulse)
	// Nothing drives the pin, so a timeout is the correct answer -- the
	// point is that the command round-trips and is bounded.
	assert.ErrorIs(t, run(context.Background(), dev, "pulse-in", []string{strconv.Itoa(int(b.pulsePin)), "1", "10ms"}, false, &strings.Builder{}),
		tfw.StatusErrTimeout, "silent line: want TFW_ERR_TIMEOUT")
}
