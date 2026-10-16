// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"errors"
	"fmt"
	"io"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// tfwsim is the FIRMWARE, compiled for the host: the same src/proto, src/core
// and src/caps the device runs, linked against a mock HAL. So these are not
// tests against a hand-written fake that would drift the moment either side
// changed -- they are the Go SDK and the C firmware agreeing on the wire, with
// no AVR toolchain and no hardware involved.

// startSimRaw is startSim without the Device wrapper, for the one test that
// needs to write bytes Device.Do would never construct on its own.
//
// A missing binary fails the test rather than skipping it: a skipped
// cross-implementation test reports success while checking nothing, and this is
// the only test that proves the two codecs agree.
func startSimRaw(t *testing.T) io.ReadWriteCloser {
	t.Helper()
	return peer.Start(t, simproc.Software)
}

// peerBoard is what this suite has to know about the board it talks to: pin
// numbers and what the board's own profile says about them. tfwsim is compiled
// against the 328P's profile; `make hw-test` names the board on the port.
type peerBoard struct {
	// resetCause is what SYS_INFO reports after the connect. tfwsim starts
	// from power-on. A real board was just reset through DTR, and its stock
	// bootloader cleared MCUSR before the application could latch it, so the
	// honest answer is unknown (src/hal/avr/hal_sys.c).
	resetCause ResetCause
	ledPort    uint8      // the LED pin 13's port index
	ledBit     uint8      // and bit
	ledPWM     PWMSupport // what pin 13's compare unit, if any, is doing
	analog     uint8      // a pin with an ADC channel and a digital port
	adcPin     uint8      // the ADC pin the descriptor test reads
	adcChannel uint8      // adcPin's channel
	adcPort    bool       // whether adcPin has a digital port
	adcOnly    int        // how many pins are ADC inputs with no digital port
	tickPin    uint8      // a compare-unit pin whose timer is the millisecond tick
	noPWMPin   uint8      // a digital pin with no compare unit at all
}

var peerBoards = map[string]peerBoard{
	"atmega328p": {
		resetCause: ResetPowerOn,
		ledPort:    0, ledBit: 5, ledPWM: PWMNone, // PB5
		analog: 14,                                // A0, PC0
		adcPin: 20, adcChannel: 6, adcPort: false, // A6
		adcOnly: 2,               // A6 and A7
		tickPin: 5, noPWMPin: 13, // OC0B
	},
	"atmega2560": {
		resetCause: ResetUnknown,
		ledPort:    1, ledBit: 7, ledPWM: PWMSystemTick, // PB7, OC0A
		analog: 54, // A0, PF0
		adcPin: 54, adcChannel: 0, adcPort: true,
		adcOnly: 0,               // every ADC input on the Mega is also a digital pin
		tickPin: 4, noPWMPin: 22, // OC0B; PA0
	},
}

// board returns the facts for the board the suite is talking to. A board this
// table does not know fails the test: guessing its pins would test the guess.
func board(t *testing.T) (string, peerBoard) {
	t.Helper()
	target, err := peer.FromEnv()
	if err != nil {
		t.Fatal(err)
	}
	b, ok := peerBoards[target.Board]
	if !ok {
		t.Fatalf("%s=%s: no pin facts for that board in peerBoards", peer.BoardEnv, target.Board)
	}
	return target.Board, b
}

func startSim(t *testing.T, opts ...Option) *Device {
	t.Helper()
	// WithTimeout first so a caller's own option, if any, wins.
	return New(startSimRaw(t), append([]Option{WithTimeout(5 * time.Second)}, opts...)...)
}

func TestAgainstFirmwarePing(t *testing.T) {
	d := startSim(t)
	msg := []byte("pong")
	got, err := d.Ping(context.Background(), msg)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(msg) {
		t.Fatalf("Ping echoed %q, want %q", got, msg)
	}
}

func TestAgainstFirmwarePingWithMaximumPayload(t *testing.T) {
	d := startSim(t)
	msg := make([]byte, MaxPayload)
	for i := range msg {
		msg[i] = byte(i)
	}
	got, err := d.Ping(context.Background(), msg)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(msg) {
		t.Fatalf("Ping echoed %d bytes, want %d identical", len(got), len(msg))
	}
}

func TestAgainstFirmwareCaps(t *testing.T) {
	name, _ := board(t)
	d := startSim(t)
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	// The pin facts this suite asserts are the named board's; a peer that is
	// some other board would fail them for the wrong reason.
	if c.BoardID.String() != name {
		t.Errorf("board = %s, want %s", c.BoardID, name)
	}
	if c.ProtoMajor != 2 {
		t.Errorf("protocol major = %d, want 2", c.ProtoMajor)
	}
	if c.MaxPayload != MaxPayload {
		t.Errorf("device MaxPayload = %d, host constant = %d -- the two must agree or a host will send frames the device refuses",
			c.MaxPayload, MaxPayload)
	}
	if !c.Has(FeatureGPIO) {
		t.Error("GPIO not reported; the default build should have it")
	}
	// CAPS must report the crypto tier it actually has, so a verifier learns
	// what it is talking to before it decides what the evidence is worth. The
	// simulator links the software provider, which is the weakest tier and says
	// so -- no TRNG, no key isolation, and a symmetric identity whose verifier
	// could forge any response it could check.
	if c.CryptoTier != CryptoSoftware {
		t.Errorf("crypto tier = %v, want software", c.CryptoTier)
	}
	if !c.Has(FeatureAttest) {
		t.Error("attestation compiled in but not reported in CAPS")
	}
}

func TestAgainstFirmwareSysInfo(t *testing.T) {
	_, b := board(t)
	d := startSim(t)
	si, err := d.SysInfo(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if si.FreeRAM == 0 {
		t.Error("free RAM reported as zero")
	}
	if si.ResetCause != b.resetCause {
		t.Errorf("reset cause = %v, want %v", si.ResetCause, b.resetCause)
	}
	// #5: the real firmware, freshly started and talked to over a clean pipe,
	// has seen no framing errors -- this is the check that the appended
	// fields actually decode against the live binary, not only against the
	// canned replies in sysinfo_test.go.
	if si.ErrCRC != 0 || si.ErrFormat != 0 {
		t.Errorf("framer error counters = crc %d format %d, want 0 0 on a clean link",
			si.ErrCRC, si.ErrFormat)
	}
}

// APP_INFO, #51, against the firmware with test/app/example linked: what the C
// runner reports decodes to what the example's descriptor says. Overruns is
// left out -- it counts real polls against a real clock.
// Not a TestAgainstFirmware* test: it starts tfwsim-app-heartbeat itself rather
// than going through the peer seam, because the board image carries no
// application. `make hw-test` selects ^TestAgainstFirmware, so it must not match.
func TestAgainstAppSimulatorAppInfo(t *testing.T) {
	d := New(simproc.Start(t, simproc.App), WithTimeout(5*time.Second))
	ai, err := d.AppInfo(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	ai.Overruns = 0
	want := AppInfo{
		State:        AppRunning,
		ABI:          1,
		VersionMajor: 1,
		RequiredCaps: 1 << 0,
		Name:         "heartbeat",
	}
	if *ai != want {
		t.Errorf("AppInfo = %+v, want %+v", *ai, want)
	}
}

// An image with no application has no APP_INFO, and says so the way it says it
// for any command it was built without.
func TestAgainstFirmwareAppInfoIsAbsentWithoutAnApplication(t *testing.T) {
	d := startSim(t)
	if _, err := d.AppInfo(context.Background()); !errors.Is(err, StatusErrUnknownCmd) {
		t.Fatalf("AppInfo err = %v, want %v", err, StatusErrUnknownCmd)
	}
}

func TestAgainstFirmwareUnknownCommandIsAnswered(t *testing.T) {
	d := startSim(t)
	_, err := d.Do(context.Background(), Cmd(0x7E), nil)
	if err == nil {
		t.Fatal("expected an error for an unknown command")
	}
	// The device must ANSWER rather than ignore: a host that got silence
	// could not tell an unknown command from a dead link.
	if err.Error() != StatusErrUnknownCmd.String() {
		t.Fatalf("err = %v, want %v", err, StatusErrUnknownCmd)
	}
}

// issue #4: tfwsim links the SOFT tier (HOST_LIB_SRCS), where
// AUTH_CHALLENGE/AUTH_RESPONSE are not compiled in at all -- the owner ruling
// is that this is a real gap, not merely a refusal, so it must answer exactly
// like any other id nobody implemented on this tier: TFW_ERR_UNKNOWN_CMD, the
// same status TestAgainstFirmwareUnknownCommandIsAnswered pins for 0x7E. The
// element-tier behaviour (a real handshake) has no cross-implementation
// binary of its own yet -- tfwsim has one crypto tier -- and is covered
// host-side by test_auth_session.c against the software element instead.
func TestAgainstFirmwareAuthCommandsAreAbsentOnTheSoftTier(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if _, err := d.AuthChallenge(ctx); err == nil || err.Error() != StatusErrUnknownCmd.String() {
		t.Fatalf("AuthChallenge err = %v, want %v", err, StatusErrUnknownCmd)
	}
	// Driven at the wire directly: AuthResponse's own signing step needs a
	// crypto.Signer, which is beside the point here -- the firmware refuses
	// before it would ever parse the payload as a signature.
	if _, err := d.Do(ctx, CmdAuthResponse, make([]byte, ecdsaRawSigLen)); err == nil ||
		err.Error() != StatusErrUnknownCmd.String() {
		t.Fatalf("AuthResponse err = %v, want %v", err, StatusErrUnknownCmd)
	}
}

func TestAgainstFirmwareWrongArgumentCountIsRefused(t *testing.T) {
	d := startSim(t)
	// RESET takes exactly one byte.
	if _, err := d.Do(context.Background(), CmdReset, nil); err == nil {
		t.Fatal("expected TFW_ERR_BAD_ARGC for a missing argument")
	}
	if _, err := d.Do(context.Background(), CmdReset, []byte{0, 0}); err == nil {
		t.Fatal("expected TFW_ERR_BAD_ARGC for a surplus argument")
	}
}

// #5's fix to src/proto/transport.c made tfw_transport_poll() yield after
// ONE malformed-frame event instead of looping through an entire buffered
// burst of them, which is what stops SYS_INFO's counters reading one event
// stale. tools/tfwsim/main.c's pump() was sized to that: this drives a burst
// of malformed bytes -- well beyond what the old flat sixteen-iteration
// bound could drain in one pass -- immediately followed, in the very same
// write, by a valid PING, and checks the PING still gets answered rather
// than being left sitting in the mock's RX queue while tfwsim goes back to
// blocking on its next read().
func TestAgainstFirmwareSurvivesAMalformedBurstBeforeAValidRequest(t *testing.T) {
	conn := startSimRaw(t)

	// Forty pairs of (SOF, SOF). The framer treats the first byte of each
	// pair as a candidate SOF and the second as its LEN -- itself SOF's own
	// value, an implausible length -- so RESYNC (frame.h) reprocesses that
	// same byte as the NEXT pair's candidate SOF. Forty BAD_FORMAT events
	// from eighty bytes of pure noise, well past the four-times-smaller bound
	// pump() used to have.
	noise := make([]byte, 0, 80)
	for i := 0; i < 40; i++ {
		noise = append(noise, SOF, SOF)
	}

	req, err := EncodeRequest(0x5A, CmdPing, []byte("hi"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := conn.Write(append(noise, req...)); err != nil {
		t.Fatal(err)
	}

	// Read raw and decode by hand -- Device.Do would write its own request,
	// which is not this test. A goroutine plus a channel bounds the wait:
	// conn.Read has no deadline (simConn does not implement one, matching
	// every other test in this file), so a regression here would otherwise
	// hang rather than fail.
	done := make(chan error, 1)
	go func() {
		var buf []byte
		one := make([]byte, 256)
		for {
			n, rerr := conn.Read(one)
			if n > 0 {
				buf = append(buf, one[:n]...)
				for len(buf) > 0 {
					f, used, derr := Decode(buf)
					if derr != nil {
						break
					}
					buf = buf[used:]
					if f.IsResponse() && f.Cmd() == CmdPing && f.Seq == 0x5A {
						if string(f.Payload) != "hi" {
							done <- fmt.Errorf("PING echoed %q, want %q", f.Payload, "hi")
							return
						}
						done <- nil
						return
					}
				}
			}
			if rerr != nil {
				done <- fmt.Errorf("reading from tfwsim: %w", rerr)
				return
			}
		}
	}()

	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("the PING after the malformed burst was never answered")
	}
}

// The device announces itself on boot, and with the connect-time wait
// disabled the SDK must surface that rather than discard it: an unexpected
// boot event means the device reset underneath the host, which invalidates
// everything the host believed about its state.
func TestAgainstFirmwareBootEventIsDelivered(t *testing.T) {
	d := startSim(t, WithBootWait(0))
	if _, err := d.Ping(context.Background(), nil); err != nil {
		t.Fatal(err)
	}
	events := d.Events()
	if len(events) == 0 {
		t.Fatal("no boot event was observed")
	}
	if !events[0].IsEvent() || events[0].Payload[0] != EventBoot {
		t.Fatalf("first event = %+v, want a boot event", events[0])
	}
}

// With the connect-time wait at its default, New itself consumes tfwsim's real
// BOOT event -- the same bytes the previous test just proved the codec can
// decode -- and it must not resurface as an "unexpected reset" once Do starts
// running ordinary commands.
func TestAgainstFirmwareDefaultBootWaitConsumesTheStartupEvent(t *testing.T) {
	d := startSim(t) // default BootWait, tfwsim really does emit TFW_EVENT_BOOT
	if _, err := d.Ping(context.Background(), []byte("after boot")); err != nil {
		t.Fatal(err)
	}
	if events := d.Events(); len(events) != 0 {
		t.Fatalf("events = %+v, want none: the startup BOOT event must be consumed, not reported", events)
	}
}

// Many commands in sequence, to catch a framer that only works once -- a
// leftover byte, an uncleared buffer, a sequence number that stops advancing.
func TestAgainstFirmwareSustainedExchange(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()
	for i := 0; i < 200; i++ {
		want := []byte{byte(i), byte(i >> 8)}
		got, err := d.Ping(ctx, want)
		if err != nil {
			t.Fatalf("iteration %d: %v", i, err)
		}
		if string(got) != string(want) {
			t.Fatalf("iteration %d: echoed %x, want %x", i, got, want)
		}
	}
}

// THE MVP, end to end: a pin driven high, reverting on the device's own
// schedule with no host involvement. The device owns the deadline, so
// a crashed or unplugged host cannot leave the pin latched.
func TestAgainstFirmwareTimerRevertsWithoutTheHost(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if _, err := d.TimerSet(ctx, 2, true, 5*time.Second); err != nil {
		t.Fatal(err)
	}
	high, err := d.DigitalRead(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	if !high {
		t.Fatal("pin did not go high when the timer was armed")
	}

	st, err := d.TimerQuery(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	if !st.Active || !st.HeldLevel {
		t.Fatalf("timer state = %+v, want active and held high", st)
	}
	if st.Remaining <= 0 || st.Remaining > 5*time.Second {
		t.Fatalf("remaining = %s, want 0 < r <= 5s", st.Remaining)
	}
}

// The simulator polls the scheduler as src/main.c's superloop does. Simulated
// time advances one millisecond per write from the host, so a 20 ms timer has
// run out after enough traffic, and the pin must have reverted on the device's
// own account.
func TestAgainstFirmwareTimerActuallyReverts(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if _, err := d.TimerSet(ctx, 2, true, 20*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 50; i++ {
		if _, err := d.Ping(ctx, []byte{byte(i)}); err != nil {
			t.Fatal(err)
		}
	}
	st, err := d.TimerQuery(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	if st.Active {
		t.Fatalf("timer still active after its deadline: %+v", st)
	}
	if high, err := d.DigitalRead(ctx, 2); err != nil || high {
		t.Fatalf("pin = %v, err = %v; want low after the timer expired", high, err)
	}
}

func TestAgainstFirmwareReservedPinsAreRefused(t *testing.T) {
	d := startSim(t)
	// Pin 1 is the transmit line this conversation is happening over.
	if err := d.DigitalWrite(context.Background(), 1, true); err == nil {
		t.Fatal("expected the UART pin to be refused")
	}
}

func TestAgainstFirmwareAdcOnlyPinCannotBeDriven(t *testing.T) {
	_, b := board(t)
	d := startSim(t)
	ctx := context.Background()
	c, err := d.Caps(ctx)
	if err != nil {
		t.Fatal(err)
	}
	// Every pin the device describes as an ADC input with no digital port
	// (the 328P's A6 and A7) must refuse to be driven. The count is the
	// board's, so a board with none, the Mega, proves it has none rather than
	// passing on an empty list.
	found := 0
	for pin := uint8(0); pin < c.PinCount; pin++ {
		info, err := d.PinInfo(ctx, pin)
		if err != nil {
			t.Fatalf("pin %d: %v", pin, err)
		}
		if !info.HasADC || info.HasPort {
			continue
		}
		found++
		if err := d.DigitalWrite(ctx, pin, true); err == nil {
			t.Errorf("pin %d is ADC-only and was driven", pin)
		}
	}
	if found != b.adcOnly {
		t.Fatalf("%d ADC-only pins, want %d", found, b.adcOnly)
	}
}

// The protected region, end to end. This is the one that matters: without the
// device-side refusal, a generic EEPROM primitive defeats the root of trust.
func TestAgainstFirmwareReservedEEPROMIsRefused(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if err := d.EEPROMWrite(ctx, 0x000, []byte{0, 0, 0, 0}); err == nil {
		t.Fatal("a write over the bootloader pubkey hash was accepted")
	}
	// A write starting below the boundary and running through it must be
	// refused whole, not partially applied. EEPROMReservedEnd grew from 0x80
	// to 0xC0 when issue #4 added the operator public key to this region.
	if err := d.EEPROMWrite(ctx, EEPROMReservedEnd-1, []byte{1, 2, 3, 4, 5, 6, 7, 8}); err == nil {
		t.Fatal("a write straddling the boundary was accepted")
	}
	// And the user area still works.
	if err := d.EEPROMWrite(ctx, 0x100, []byte{0xDE, 0xAD}); err != nil {
		t.Fatalf("user-area write refused: %v", err)
	}
	got, err := d.EEPROMRead(ctx, 0x100, 2)
	if err != nil {
		t.Fatal(err)
	}
	if got[0] != 0xDE || got[1] != 0xAD {
		t.Fatalf("read back %x, want dead", got)
	}
}

// issue #4: on the soft tier, a read of the reserved region is still
// unconditionally permitted -- AUTH_CHALLENGE/AUTH_RESPONSE do not exist here
// to gate it, and that gap is accepted rather than hidden (see
// docs/security/threat-model.md). The element-tier refusal-then-success path
// is covered by test_auth_session.c against the software element; the
// element-tier simulator (sdk/go/tfwsim) is built without the session, as the
// 328P profile it simulates is.
func TestAgainstFirmwareReservedEEPROMReadIsUngatedOnTheSoftTier(t *testing.T) {
	d := startSim(t)
	if _, err := d.EEPROMRead(context.Background(), 0x000, 4); err != nil {
		t.Fatalf("reserved-region read refused on the soft tier: %v", err)
	}
}

// An unprovisioned device must REFUSE to attest rather than sign with a secret
// every build of this source would share. Evidence that verifies against a
// shared key proves nothing while looking exactly like evidence that proves
// something -- which is the one failure a verifier cannot detect.
func TestAgainstFirmwareUnprovisionedDeviceRefusesToAttest(t *testing.T) {
	d := startSim(t)
	nonce := make([]byte, 16)
	if _, err := d.Do(context.Background(), CmdAttest, nonce); err == nil {
		t.Fatal("an unprovisioned device produced attestation evidence")
	} else if err.Error() != StatusErrNotProvisioned.String() {
		t.Fatalf("err = %v, want %v", err, StatusErrNotProvisioned)
	}
}

func TestAgainstFirmwareAnalogReadReturnsRawCounts(t *testing.T) {
	_, b := board(t)
	d := startSim(t)
	// The mock reports zero until told otherwise; the point here is that the
	// command round-trips and the pin maps to a channel at all.
	if _, err := d.AnalogRead(context.Background(), b.analog); err != nil {
		t.Fatalf("A0 (pin %d): %v", b.analog, err)
	}
	if _, err := d.AnalogRead(context.Background(), 13); err == nil {
		t.Fatal("expected a digital-only pin to have no analog reading")
	}
}

func TestAgainstFirmwarePWM(t *testing.T) {
	_, b := board(t)
	d := startSim(t)
	ctx := context.Background()

	if err := d.PWMWrite(ctx, 9, 128); err != nil {
		t.Fatalf("pin 9 has a free compare unit and should accept a duty: %v", err)
	}
	// Timer0 is the millisecond tick, so its compare pins have units that are
	// spent (5 and 6 on the 328P, 4 and 13 on the Mega). The refusal is what
	// stops a host writing a duty into a timer counting something else and
	// wondering why nothing moves.
	if err := d.PWMWrite(ctx, b.tickPin, 128); err == nil {
		t.Errorf("pin %d should refuse PWM: its timer drives the tick", b.tickPin)
	}
	if err := d.PWMWrite(ctx, b.noPWMPin, 128); err == nil {
		t.Errorf("pin %d has no compare unit and should refuse PWM", b.noPWMPin)
	}
}

// The secure element's address must be unreachable through the generic bus
// primitive, or the device's own root of trust is an ordinary peripheral.
func TestAgainstFirmwareSecureElementAddressIsRefused(t *testing.T) {
	d := startSim(t)
	if _, err := d.I2CTransfer(context.Background(), I2CReservedAddr, []byte{0}, 1); err == nil {
		t.Fatal("the secure element's I2C address was reachable")
	} else if err.Error() != StatusErrProtectedRegion.String() {
		t.Fatalf("err = %v, want %v", err, StatusErrProtectedRegion)
	}
}

func TestAgainstFirmwareOneWireAndPulseAreReachable(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()
	if _, err := d.OneWireReset(ctx, 9); err != nil {
		t.Fatalf("1-Wire reset: %v", err)
	}
	// No signal on a simulated pin, so a timeout is the correct answer -- the
	// point is that the command round-trips and is bounded.
	if _, err := d.PulseIn(ctx, 9, true, 10*time.Millisecond); err == nil {
		t.Error("expected a timeout on a silent line")
	}
}

// CAPS reports which algorithms the provider can PERFORM, which is not implied
// by the tier: the simulator links the software provider, and that provider
// has no asymmetric key at all. Claiming ECDSA here would be the defect it
// exists to avoid -- a host negotiating down to something the device then
// fails to do, at the point where it matters most.
func TestAgainstFirmwareCapsReportsTheAlgorithmsItCanPerform(t *testing.T) {
	d := startSim(t)
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if c.Algorithms != AlgHMACSHA256 {
		t.Fatalf("algorithms = %s (%#04x), want hmac-sha256 alone", c.Algorithms, uint16(c.Algorithms))
	}
	if c.Algorithms.Can(AlgECDSAP256SHA256) || c.Algorithms.Can(AlgEd25519) {
		t.Error("the software provider claimed an asymmetric algorithm it cannot perform")
	}
}

// CAPS answers per-pin pages, and this is the check that the six bytes the
// firmware writes are the six bytes this SDK reads (#12: port and bit a whole
// byte each). The board is the real
// ATmega328P table, compiled and linked into the simulator.
func TestAgainstFirmwarePinInfo(t *testing.T) {
	_, b := board(t)
	d := startSim(t)
	ctx := context.Background()

	led, err := d.PinInfo(ctx, 13)
	if err != nil {
		t.Fatal(err)
	}
	if led.Pin != 13 {
		t.Errorf("descriptor echoed pin %d, want 13", led.Pin)
	}
	if !led.Flags.Has(PinDigital | PinLED) {
		t.Errorf("pin 13 flags = %s, want digital and led", led.Flags)
	}
	if !led.HasPort || led.Port != b.ledPort || led.Bit != b.ledBit {
		t.Errorf("pin 13 = port %d bit %d (present %v), want port %d bit %d",
			led.Port, led.Bit, led.HasPort, b.ledPort, b.ledBit)
	}
	if led.HasADC {
		t.Error("pin 13 has no analog channel")
	}
	// None on the 328P, which has no compare unit there; on the Mega it is
	// OC0A, spent on the tick.
	if led.PWM != b.ledPWM {
		t.Errorf("pin 13 pwm = %s, want %s", led.PWM, b.ledPWM)
	}

	// Pin 9 is OC1A on the 328P and OC2B on the Mega; neither timer is the tick.
	pwm, err := d.PinInfo(ctx, 9)
	if err != nil {
		t.Fatal(err)
	}
	if pwm.PWM != PWMAvailable {
		t.Errorf("pin 9 pwm = %s, want available", pwm.PWM)
	}
	if !pwm.Flags.Has(PinPWM) {
		t.Errorf("pin 9 flags = %s, want pwm", pwm.Flags)
	}

	// The 328P's A6 exists as an analog input and has no digital port at all,
	// which the descriptor must say rather than report port 0 bit 0 -- a pin
	// that can be read and never written. The Mega's A0 has both.
	adc, err := d.PinInfo(ctx, b.adcPin)
	if err != nil {
		t.Fatal(err)
	}
	if adc.HasPort != b.adcPort {
		t.Errorf("pin %d digital port present = %v, want %v", b.adcPin, adc.HasPort, b.adcPort)
	}
	if !adc.HasADC || adc.ADCChannel != b.adcChannel {
		t.Errorf("pin %d adc = %d (present %v), want channel %d", b.adcPin, adc.ADCChannel, adc.HasADC, b.adcChannel)
	}
	if !adc.Flags.Has(PinADC) || adc.Flags.Has(PinDigital) != b.adcPort {
		t.Errorf("pin %d flags = %s, want adc, and digital only with a port", b.adcPin, adc.Flags)
	}

	// The UART pins are in use by this build; a host must be able to see that
	// before it drives one and cuts the line it is talking over.
	tx, err := d.PinInfo(ctx, 1)
	if err != nil {
		t.Fatal(err)
	}
	if !tx.Flags.Has(PinInUse) {
		t.Errorf("pin 1 flags = %s, want in-use", tx.Flags)
	}
}

func TestAgainstFirmwarePinInfoRefusesAPinTheBoardLacks(t *testing.T) {
	d := startSim(t)
	_, err := d.PinInfo(context.Background(), 200)
	if err == nil {
		t.Fatal("expected a refusal for a pin off the end of the board")
	}
	if err.Error() != StatusErrBadPin.String() {
		t.Fatalf("err = %v, want %v", err, StatusErrBadPin)
	}
}

// I2C_CONFIG and SPI_CONFIG (issue #21): the host can now choose what
// tfw_bus_init() previously fixed at boot. A value within range reaches the
// device; one outside it is refused before a frame is even sent.
func TestAgainstFirmwareI2CConfigAppliesAClockWithinRange(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if err := d.SetI2CClock(ctx, 400_000); err != nil {
		t.Fatalf("a clock within range should be accepted: %v", err)
	}

	if err := d.SetI2CClock(ctx, 0); err == nil {
		t.Fatal("a zero clock was accepted")
	} else {
		var ae *ArgRangeError
		if !errors.As(err, &ae) {
			t.Fatalf("want *ArgRangeError, got %T: %v", err, err)
		}
	}
}

func TestAgainstFirmwareSPIConfigAppliesModeClkdivAndBitOrder(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if err := d.ConfigureSPI(ctx, SPIMode3, SPIClockDiv16, true); err != nil {
		t.Fatalf("a valid configuration should be accepted: %v", err)
	}

	if err := d.ConfigureSPI(ctx, SPIMode(4), SPIClockDiv4, false); err == nil {
		t.Fatal("a mode past 3 was accepted")
	} else {
		var ae *ArgRangeError
		if !errors.As(err, &ae) {
			t.Fatalf("want *ArgRangeError, got %T: %v", err, err)
		}
	}
}
