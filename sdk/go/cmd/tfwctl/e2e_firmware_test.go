// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"context"
	"fmt"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
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
}

var peerBoards = map[string]peerBoard{
	"atmega328p": {
		resetCause: tfw.ResetPowerOn,
		ledPort:    0, ledBit: 5, ledPWM: tfw.PWMNone, // PB5
		analog: 14,                                // A0, PC0
		adcPin: 20, adcChannel: 6, adcPort: false, // A6
		adcOnly: 2,               // A6 and A7
		tickPin: 5, noPWMPin: 13, // OC0B
	},
	"atmega2560": {
		resetCause: tfw.ResetUnknown,
		ledPort:    1, ledBit: 7, ledPWM: tfw.PWMSystemTick, // PB7, OC0A
		analog: 54, // A0, PF0
		adcPin: 54, adcChannel: 0, adcPort: true,
		adcOnly: 0,               // every ADC input on the Mega is also a digital pin
		tickPin: 4, noPWMPin: 22, // OC0B; PA0
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
	dev := startFirmware(t, tfw.WithBootWait(0))
	var got pingJSON
	cliJSON(t, dev, &got, "ping")
	events := dev.Events()
	require.NotEmpty(t, events, "no boot event was observed")
	assert.True(t, events[0].IsEvent() && events[0].Payload[0] == tfw.EventBoot,
		"first event = %+v, want a boot event", events[0])
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
	assert.Equal(t, b.resetCause, si.ResetCause, "reset cause")
	// #5: freshly started and talked to over a clean link, the firmware has
	// seen no framing errors -- the appended fields decode against the live
	// device, not only against canned replies.
	assert.Zero(t, si.ErrCRC, "framer CRC errors on a clean link")
	assert.Zero(t, si.ErrFormat, "framer format errors on a clean link")
}

func TestFirmwarePin(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)

	led := cliPin(t, dev, 13)
	assert.EqualValues(t, 13, led.Pin, "descriptor echoed the pin")
	assert.True(t, led.Flags.Has(tfw.PinDigital|tfw.PinLED), "pin 13 flags = %s, want digital and led", led.Flags)
	assert.True(t, led.HasPort, "pin 13 has a port")
	assert.Equal(t, b.ledPort, led.Port, "pin 13 port")
	assert.Equal(t, b.ledBit, led.Bit, "pin 13 bit")
	assert.False(t, led.HasADC, "pin 13 has no analog channel")
	// None on the 328P, which has no compare unit there; on the Mega it is
	// OC0A, spent on the tick.
	assert.Equal(t, b.ledPWM, led.PWM, "pin 13 pwm")

	// Pin 9 is OC1A on the 328P and OC2B on the Mega; neither timer is the tick.
	pwm := cliPin(t, dev, 9)
	assert.Equal(t, tfw.PWMAvailable, pwm.PWM, "pin 9 pwm")
	assert.True(t, pwm.Flags.Has(tfw.PinPWM), "pin 9 flags = %s, want pwm", pwm.Flags)

	// The 328P's A6 exists as an analog input and has no digital port at all,
	// which the descriptor must say rather than report port 0 bit 0 -- a pin
	// that can be read and never written. The Mega's A0 has both.
	adc := cliPin(t, dev, b.adcPin)
	assert.Equal(t, b.adcPort, adc.HasPort, "pin %d digital port present", b.adcPin)
	assert.True(t, adc.HasADC, "pin %d has an adc channel", b.adcPin)
	assert.Equal(t, b.adcChannel, adc.ADCChannel, "pin %d adc channel", b.adcPin)
	assert.True(t, adc.Flags.Has(tfw.PinADC), "pin %d flags = %s, want adc", b.adcPin, adc.Flags)
	assert.Equal(t, b.adcPort, adc.Flags.Has(tfw.PinDigital), "pin %d flags = %s, digital only with a port", b.adcPin, adc.Flags)

	// The UART pins are in use by this build; a host must be able to see that
	// before it drives one and cuts the line it is talking over.
	tx := cliPin(t, dev, 1)
	assert.True(t, tx.Flags.Has(tfw.PinInUse), "pin 1 flags = %s, want in-use", tx.Flags)

	assert.Equal(t, tfw.StatusErrBadPin, cliStatus(t, dev, "pin", "200"), "a pin off the end of the board")
}

func TestFirmwareWrite(t *testing.T) {
	_, b := firmwareBoard(t)
	dev := startFirmware(t)
	// Pin 1 is the transmit line this conversation is happening over.
	cliRefused(t, dev, "write", "1", "1")

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
	dev := startFirmware(t)
	var st tfw.TimerState

	require.NoError(t, run(context.Background(), dev, "timer", []string{"2", "1", "5s"}, false, &strings.Builder{}))
	var level struct {
		High bool `json:"high"`
	}
	cliJSON(t, dev, &level, "read", "2")
	require.True(t, level.High, "pin did not go high when the timer was armed")
	cliJSON(t, dev, &st, "timer-query", "2")
	require.True(t, st.Active && st.HeldLevel, "timer state = %+v, want active and held high", st)
	require.True(t, st.Remaining > 0 && st.Remaining <= 5*time.Second, "remaining = %s, want 0 < r <= 5s", st.Remaining)

	require.NoError(t, run(context.Background(), dev, "timer", []string{"2", "1", "20ms"}, false, &strings.Builder{}))
	// A wait on the condition, bounded by exchanges: each timer-query is one
	// round trip, and five hundred of them outlast 20 ms on every link.
	const bound = 500
	queries := 0
	for st.Active = true; st.Active && queries < bound; queries++ {
		cliJSON(t, dev, &st, "timer-query", "2")
	}
	require.False(t, st.Active, "wait: the device reverts pin 2's 20ms timer, within %d timer-query exchanges", bound)
	cliJSON(t, dev, &level, "read", "2")
	assert.False(t, level.High, "pin high after the timer expired")
}

// EEPROMReservedEnd grew from 0x80 to 0xC0 when issue #4 added the operator
// public key to the reserved region.
func TestFirmwareEEPROM(t *testing.T) {
	dev := startFirmware(t)
	cliRefused(t, dev, "eeprom-write", "0", "00000000") // over the bootloader pubkey hash
	// A write starting below the boundary and running through it is refused
	// whole, not partially applied.
	cliRefused(t, dev, "eeprom-write", strconv.Itoa(tfw.EEPROMReservedEnd-1), "0102030405060708")

	// And the user area still works.
	require.NoError(t, run(context.Background(), dev, "eeprom-write", []string{"0x100", "dead"}, false, &strings.Builder{}))
	var got struct {
		Hex string `json:"hex"`
	}
	cliJSON(t, dev, &got, "eeprom-read", "0x100", "2")
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
	assert.Equal(t, tfw.StatusErrProtectedRegion,
		cliStatus(t, dev, "i2c", strconv.Itoa(tfw.I2CReservedAddr), "00", "1"),
		"the secure element's I2C address was reachable")
}

func TestFirmwareI2CConfig(t *testing.T) {
	dev := startFirmware(t)
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
	require.NoError(t, run(ctx, dev, "spi-config", []string{mode3, div16, "lsb"}, false, &strings.Builder{}),
		"a valid configuration should be accepted")
	var ae *tfw.ArgRangeError
	require.ErrorAs(t, run(ctx, dev, "spi-config", []string{"4", fmt.Sprint(uint8(tfw.SPIClockDiv4)), "msb"}, false, &strings.Builder{}),
		&ae, "a mode past 3")
}

func TestFirmwareOneWireAndPulseIn(t *testing.T) {
	dev := startFirmware(t)
	var reset struct {
		Present bool `json:"present"`
	}
	cliJSON(t, dev, &reset, "1wire-reset", "9")
	// No signal on a simulated pin, so a timeout is the correct answer -- the
	// point is that the command round-trips and is bounded.
	assert.Error(t, run(context.Background(), dev, "pulse-in", []string{"9", "1", "10ms"}, false, &strings.Builder{}),
		"expected a timeout on a silent line")
}
