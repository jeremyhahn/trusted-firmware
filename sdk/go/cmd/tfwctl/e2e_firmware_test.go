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

// peerBoard is what the scenarios have to know about the board they talk to:
// pin numbers and what the board's own profile says about them. tfwsim is
// compiled against the 328P's profile; `make hw-test` names the board on the
// port.
type peerBoard struct {
	// resetCause is what SYS_INFO reports after the connect. tfwsim starts
	// from power-on. A real board was just reset through DTR, and its stock
	// bootloader cleared MCUSR before the application could latch it, so the
	// honest answer is unknown (src/hal/avr/hal_sys.c).
	resetCause tfw.ResetCause
	// resetAny is a board whose rows do not assert resetCause: over a tcp:
	// link nothing resets it before the row, so it reports whatever its
	// last reset was (#550).
	resetAny   bool
	ledPort    uint8          // the LED pin 13's port index
	ledBit     uint8          // and bit
	ledPWM     tfw.PWMSupport // what pin 13's compare unit, if any, is doing
	analog     uint8          // a pin with an ADC channel and a digital port
	adcPin     uint8          // the ADC pin the descriptor scenario reads
	adcChannel uint8          // adcPin's channel
	adcPort    bool           // whether adcPin has a digital port
	adcOnly    int            // how many pins are ADC inputs with no digital port
	tickPin    uint8          // a compare-unit pin whose timer is the millisecond tick
	noPWMPin   uint8          // a digital pin with no compare unit at all
	ledPin     uint8          // the pin the board's table flags as its LED
	txPin      int            // the UART's transmit pin; -1 where the UART is not in the pin table
	timerPin   uint8          // a digital output the timer scenario drives and reads back
}

var peerBoards = map[string]peerBoard{
	"atmega328p": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 5, ledPWM: tfw.PWMNone, // PB5
		analog: 14,                                // A0, PC0
		adcPin: 20, adcChannel: 6, adcPort: false, // A6
		adcOnly: 2,               // A6 and A7
		tickPin: 5, noPWMPin: 13, // OC0B
		ledPin: 13, txPin: 1, timerPin: 2,
	},
	"atmega2560": {
		resetCause: tfw.ResetUnknown,
		ledPort:    1, ledBit: 7, ledPWM: tfw.PWMSystemTick, // PB7, OC0A
		analog: 54, // A0, PF0
		adcPin: 54, adcChannel: 0, adcPort: true,
		adcOnly: 0,               // every ADC input on the Mega is also a digital pin
		tickPin: 4, noPWMPin: 22, // OC0B; PA0
		ledPin: 13, txPin: 1, timerPin: 2,
	},
	// The MPS2 boards QEMU models (boards/mps2-an505/board.c and
	// boards/mps2-an500/board.c, the same table): three pins, user LEDs 0 and
	// 1 on the FPGA I/O block's port 0, outputs only, and pin 2 the shield 0
	// SPI select (MISC, port 1 bit 8), no flags; no ADC, no PWM, and the
	// UART outside the pin table. QEMU's system_reset leaves no reset mark
	// behind, so the HAL reads power-on (src/hal/cortex-m/hal_early.c). The
	// analog, PWM, EEPROM, bus and pulse rows find their families absent from
	// CAPS and fail (requireFeature), so the QEMU run leaves them out; their
	// pin fields here are never read.
	"mps2-an505": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 0, ledPWM: tfw.PWMNone, // LED0
		ledPin: 0, txPin: -1, timerPin: 1, // LED1
	},
	"mps2-an500": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 0, ledPWM: tfw.PWMNone, // LED0
		ledPin: 0, txPin: -1, timerPin: 1, // LED1
	},
	// The ESP32-P4 PoE board (boards/esp32p4-poe/board.c): seventeen header
	// pins on two banks, ADC1 channels 4..7 on pins 7..10, each also a
	// digital pin; LEDC0..3 on pins 0..3. The wire is a TCP server
	// (hal_uart.c), so the UART is not in the pin table. The table flags no
	// pin as an LED and no LEDC channel is the millisecond tick (esp_timer
	// is): the LED and tick fields are pin 0's, and the Pin and PWM rows
	// that read them are not yet confirmed on this board.
	"esp32p4-poe": {
		resetAny: true,
		ledPort:  0, ledBit: 2, ledPWM: tfw.PWMNone, // pin 0, GPIO2
		analog: 7, // GPIO20, ADC1_CH4
		adcPin: 7, adcChannel: 4, adcPort: true,
		adcOnly: 0,
		tickPin: 0, noPWMPin: 4, // GPIO2; GPIO6
		ledPin: 0, txPin: -1, timerPin: 11, // GPIO32
	},
}

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

// startFirmware opens the device the seam names. A missing simulator binary or
// a port that does not answer fails the scenario; neither is a skip.
func startFirmware(t *testing.T, opts ...tfw.Option) *tfw.Device {
	t.Helper()
	// WithTimeout first so a caller's own option, if any, wins.
	return tfw.New(peer.Start(t, simproc.Software), append([]tfw.Option{tfw.WithTimeout(5 * time.Second)}, opts...)...)
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

func TestFirmwarePingDeliversTheBootEventWithNoBootWait(t *testing.T) {
	target, err := peer.FromEnv()
	require.NoError(t, err)
	conn, skip := bootEventLink(t, target, os.Getenv(resetPortEnv))
	if skip != "" {
		t.Skip(skip)
	}
	assertBootEvent(t, conn)
}

// assertBootEvent pings over conn with no boot wait and asserts the first
// event the device delivered is its boot event.
func assertBootEvent(t *testing.T, conn io.ReadWriteCloser) {
	t.Helper()
	dev := tfw.New(conn, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0))
	var got pingJSON
	cliJSON(t, dev, &got, "ping")
	events := dev.Events()
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
	r, ok := p.(modemLines)
	if !ok {
		return fmt.Errorf("%s cannot drive RTS and DTR", port)
	}
	if err := espReset(r); err != nil {
		return fmt.Errorf("%s: %w", port, err)
	}
	return nil
}

// modemLines is what espReset needs of the reset port.
type modemLines interface {
	SetDTR(bool) error
	SetRTS(bool) error
}

// espReset is esptool's classic reset on the ESP32 auto-reset circuit
// (docs/plans/550.md): RTS drives EN and DTR drives GPIO0, each asserted
// line pulling its pin low, and both asserted together pull neither. DTR is
// released first so GPIO0 is high when EN rises (a normal boot, not the ROM
// download mode); RTS then holds EN low for peer.ResetPulse and releases it.
// Pulsing DTR alone never moves EN, so it does not reset a running board.
func espReset(r modemLines) error {
	if err := r.SetDTR(false); err != nil {
		return fmt.Errorf("releasing DTR (GPIO0 high): %w", err)
	}
	if err := r.SetRTS(true); err != nil {
		return fmt.Errorf("asserting RTS (EN low): %w", err)
	}
	time.Sleep(peer.ResetPulse)
	if err := r.SetRTS(false); err != nil {
		return fmt.Errorf("releasing RTS (EN high): %w", err)
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
	deadline := time.Now().Add(peer.StartBound)
	for {
		c, err := peer.Open(target.Port, peer.StartBound)
		if err == nil {
			t.Cleanup(func() { _ = c.Close() })
			return c, ""
		}
		require.True(t, time.Now().Before(deadline), "%s did not answer within %v of the reset: %v", target.Port, peer.StartBound, err)
		time.Sleep(peer.ResetPulse)
	}
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

// linePort records the modem-line changes espReset makes, and when.
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
	require.NoError(t, espReset(l))
	require.Equal(t, []string{"DTR=false", "RTS=true", "RTS=false"}, l.calls)
	assert.GreaterOrEqual(t, l.at[2].Sub(l.at[1]), peer.ResetPulse, "EN held low too briefly")
}

// A line that will not move is an error, and the sequence stops there.
func TestESPResetReportsALineThatFails(t *testing.T) {
	for _, fail := range []string{"DTR=false", "RTS=true", "RTS=false"} {
		l := &linePort{fail: fail}
		err := espReset(l)
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
	assert.True(t, c.Has(tfw.FeatureAttest), "attestation compiled in but not reported in CAPS")
	// CAPS must report the crypto tier it actually has, so a verifier learns
	// what it is talking to before it decides what the evidence is worth
	// (docs/wire-protocol/api.md:63), and what it can sign with:
	// docs/dice-attestation/user.md, a SOFTWARE-tier device signs ES256 under
	// its P-256 DeviceID (SOFT_ID=p256: tfwsim and the ATmega2560), except the
	// ATmega328P and ATmega32U4 images, which keep the HMAC-SHA256 signer
	// (SOFT_ID=hmac); docs/dice-attestation/cli.md, the element performs
	// ECDSA-P256-SHA256 and HMAC-SHA256. Neither performs Ed25519.
	target, err := peer.FromEnv()
	require.NoError(t, err)
	soft := tfw.AlgECDSAP256SHA256
	if target.Hardware() && (target.Board == "atmega328p" || target.Board == "atmega32u4") {
		soft = tfw.AlgHMACSHA256
	}
	switch tier := c.CryptoTier.String(); tier {
	case "software":
		assert.Equal(t, soft, c.Algorithms, "algorithms = %s, want %s alone", c.Algorithms, soft)
	case "secure-element":
		assert.Equal(t, tfw.AlgHMACSHA256|tfw.AlgECDSAP256SHA256, c.Algorithms,
			"algorithms = %s, want ecdsa-p256-sha256 and hmac-sha256", c.Algorithms)
	default:
		t.Fatalf("crypto tier %s: no facts for it", tier)
	}
	assert.False(t, c.Algorithms.Can(tfw.AlgEd25519), "the provider claimed Ed25519, which neither tier can perform")
}

func TestFirmwareSys(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	var si tfw.SysInfo
	cliJSON(t, dev, &si, "sys")
	assert.NotZero(t, si.FreeRAM, "free RAM reported as zero")
	if !b.resetAny {
		assert.Equal(t, b.resetCause, si.ResetCause, "reset cause")
	}
	// #5: talked to over a clean link, the firmware counts no framing errors
	// -- the appended fields decode against the live device, not only
	// against canned replies. #550: a delta, not zero, since over a tcp: link
	// nothing resets the board and an earlier row's burst still counts.
	sysCountersHold(t, dev, si)
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
// malformed burst first and was never reset. The row passes, and the
// counters the burst raised are still raised after it.
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
	t.Setenv(peer.PortEnv, peer.TCPPrefix+ln.Addr().String())
	t.Setenv(peer.BoardEnv, "esp32p4-poe")

	require.True(t, t.Run("TestFirmwareSys", TestFirmwareSys), "the Sys row failed on the ESP32 over tcp:")

	// The row's host has gone; the next one is accepted once the wire has
	// let it go.
	var si *tfw.SysInfo
	require.Eventually(t, func() bool {
		c, err := net.Dial("tcp", ln.Addr().String())
		if err != nil {
			return false
		}
		defer func() { _ = c.Close() }()
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		si, err = tfw.New(c, tfw.WithTimeout(time.Second)).SysInfo(ctx)
		return err == nil
	}, 10*time.Second, 50*time.Millisecond)
	assert.NotZero(t, si.ErrCRC+si.ErrFormat, "the burst raised no counter, so the row proved nothing")
}

func TestFirmwarePin(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	caps := cliCaps(t, dev)

	led := cliPin(t, dev, b.ledPin)
	assert.EqualValues(t, b.ledPin, led.Pin, "descriptor echoed the pin")
	assert.True(t, led.Flags.Has(tfw.PinDigital|tfw.PinLED), "pin %d flags = %s, want digital and led", b.ledPin, led.Flags)
	assert.True(t, led.HasPort, "pin %d has a port", b.ledPin)
	assert.Equal(t, b.ledPort, led.Port, "pin %d port", b.ledPin)
	assert.Equal(t, b.ledBit, led.Bit, "pin %d bit", b.ledPin)
	assert.False(t, led.HasADC, "pin %d has no analog channel", b.ledPin)
	// None on the 328P, which has no compare unit there; on the Mega it is
	// OC0A, spent on the tick.
	assert.Equal(t, b.ledPWM, led.PWM, "pin %d pwm", b.ledPin)

	// Pin 9 is OC1A on the 328P and OC2B on the Mega; neither timer is the tick.
	if caps.Has(tfw.FeaturePWM) {
		pwm := cliPin(t, dev, 9)
		assert.Equal(t, tfw.PWMAvailable, pwm.PWM, "pin 9 pwm")
		assert.True(t, pwm.Flags.Has(tfw.PinPWM), "pin 9 flags = %s, want pwm", pwm.Flags)
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
		assert.Equal(t, b.adcPort, adc.Flags.Has(tfw.PinDigital), "pin %d flags = %s, digital only with a port", b.adcPin, adc.Flags)
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

	// docs/device-provisioning/engineering.md:110: the software tier reads the
	// reserved region; the element tier gates the read behind the operator
	// session.
	err := run(context.Background(), dev, "eeprom-read", []string{"0", "4"}, false, &strings.Builder{})
	switch tier := cliTier(t, dev); tier {
	case "software":
		assert.NoError(t, err, "reserved-region read refused on the soft tier")
	case "secure-element":
		assert.Error(t, err, "reserved-region read answered on the element tier without an operator session")
	default:
		t.Fatalf("crypto tier %s: no reserved-read facts for it", tier)
	}
}

func TestFirmwareAttestRefusedUnprovisioned(t *testing.T) {
	dev := startFirmware(t)
	nonce := strings.Repeat("00", tfw.AttestNonceLen)
	assert.Equal(t, tfw.StatusErrNotProvisioned, cliStatus(t, dev, "attest", nonce),
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
	cliRefused(t, dev, "analog", "13") // digital-only
}

func TestFirmwarePWM(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeaturePWM)
	require.NoError(t, run(context.Background(), dev, "pwm", []string{"9", "128"}, false, &strings.Builder{}),
		"pin 9 has a free compare unit and should accept a duty")
	// Timer0 is the millisecond tick, so its compare pins have units that are
	// spent (5 and 6 on the 328P, 4 and 13 on the Mega). The refusal is what
	// stops a host writing a duty into a timer counting something else and
	// wondering why nothing moves.
	cliRefused(t, dev, "pwm", strconv.Itoa(int(b.tickPin)), "128")
	cliRefused(t, dev, "pwm", strconv.Itoa(int(b.noPWMPin)), "128") // no compare unit
}

func TestFirmwareI2C(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureI2C)
	assert.Equal(t, tfw.StatusErrProtectedRegion,
		cliStatus(t, dev, "i2c", strconv.Itoa(tfw.I2CReservedAddr), "00", "1"),
		"the secure element's I2C address was reachable")
}

func TestFirmwareI2CConfig(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureI2C)
	ctx := context.Background()
	// src/crypto/atecc608a/atecc.c:61: I2C_CONFIG refuses to touch the TWI
	// peripheral on an element build; the software tier applies the clock.
	switch tier := cliTier(t, dev); tier {
	case "software":
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

func TestFirmwareOneWireAndPulseIn(t *testing.T) {
	dev := startFirmware(t)
	requireFeature(t, dev, tfw.FeatureOneWire)
	requireFeature(t, dev, tfw.FeaturePulse)
	var reset struct {
		Present bool `json:"present"`
	}
	cliJSON(t, dev, &reset, "1wire-reset", "9")
	// No signal on a simulated pin, so a timeout is the correct answer -- the
	// point is that the command round-trips and is bounded.
	assert.ErrorIs(t, run(context.Background(), dev, "pulse-in", []string{"9", "1", "10ms"}, false, &strings.Builder{}),
		tfw.StatusErrTimeout, "silent line: want TFW_ERR_TIMEOUT")
}
