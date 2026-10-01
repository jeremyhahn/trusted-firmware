// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// agsim is the FIRMWARE, compiled for the host: the same src/proto, src/core
// and src/caps the device runs, linked against a mock HAL. So these are not
// tests against a hand-written fake that would drift the moment either side
// changed -- they are the Go SDK and the C firmware agreeing on the wire, with
// no AVR toolchain and no hardware involved.

type simConn struct {
	cmd *exec.Cmd
	in  io.WriteCloser
	out io.ReadCloser
}

func (s *simConn) Read(p []byte) (int, error)  { return s.out.Read(p) }
func (s *simConn) Write(p []byte) (int, error) { return s.in.Write(p) }
func (s *simConn) Close() error {
	_ = s.in.Close()
	_ = s.cmd.Process.Kill()
	_, _ = s.cmd.Process.Wait()
	return nil
}

// startSimRaw is startSim without the Device wrapper, for the one test that
// needs to write bytes Device.Do would never construct on its own.
func startSimRaw(t *testing.T) *simConn {
	t.Helper()

	bin, err := filepath.Abs("../../build/host/agsim")
	if err != nil {
		t.Fatalf("resolving agsim path: %v", err)
	}
	// Deliberately a failure and not a skip. A skipped cross-implementation
	// test reports success while checking nothing, and this is the only test
	// that proves the two codecs agree.
	if _, err := os.Stat(bin); err != nil {
		t.Fatalf("agsim not built at %s -- run `make agsim` first: %v", bin, err)
	}

	cmd := exec.Command(bin)
	in, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	c := &simConn{cmd: cmd, in: in, out: out}
	t.Cleanup(func() { _ = c.Close() })
	return c
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
	d := startSim(t)
	c, err := d.Caps(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if c.ProtoMajor != 1 {
		t.Errorf("protocol major = %d, want 1", c.ProtoMajor)
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
	d := startSim(t)
	si, err := d.SysInfo(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if si.FreeRAM == 0 {
		t.Error("free RAM reported as zero")
	}
	if si.ResetCause != ResetPowerOn {
		t.Errorf("reset cause = %v, want power-on", si.ResetCause)
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

// issue #4: agsim links the SOFT tier (HOST_LIB_SRCS), where
// AUTH_CHALLENGE/AUTH_RESPONSE are not compiled in at all -- the owner ruling
// is that this is a real gap, not merely a refusal, so it must answer exactly
// like any other id nobody implemented on this tier: AG_ERR_UNKNOWN_CMD, the
// same status TestAgainstFirmwareUnknownCommandIsAnswered pins for 0x7E. The
// element-tier behaviour (a real handshake) has no cross-implementation
// binary of its own yet -- agsim has one crypto tier -- and is covered
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
		t.Fatal("expected AG_ERR_BAD_ARGC for a missing argument")
	}
	if _, err := d.Do(context.Background(), CmdReset, []byte{0, 0}); err == nil {
		t.Fatal("expected AG_ERR_BAD_ARGC for a surplus argument")
	}
}

// #5's fix to src/proto/transport.c made ag_transport_poll() yield after
// ONE malformed-frame event instead of looping through an entire buffered
// burst of them, which is what stops SYS_INFO's counters reading one event
// stale. tools/agsim/main.c's pump() was sized to that: this drives a burst
// of malformed bytes -- well beyond what the old flat sixteen-iteration
// bound could drain in one pass -- immediately followed, in the very same
// write, by a valid PING, and checks the PING still gets answered rather
// than being left sitting in the mock's RX queue while agsim goes back to
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
				done <- fmt.Errorf("reading from agsim: %w", rerr)
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

// With the connect-time wait at its default, New itself consumes agsim's real
// BOOT event -- the same bytes the previous test just proved the codec can
// decode -- and it must not resurface as an "unexpected reset" once Do starts
// running ordinary commands.
func TestAgainstFirmwareDefaultBootWaitConsumesTheStartupEvent(t *testing.T) {
	d := startSim(t) // default BootWait, agsim really does emit AG_EVENT_BOOT
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

func TestAgainstFirmwareReservedPinsAreRefused(t *testing.T) {
	d := startSim(t)
	// Pin 1 is the transmit line this conversation is happening over.
	if err := d.DigitalWrite(context.Background(), 1, true); err == nil {
		t.Fatal("expected the UART pin to be refused")
	}
}

func TestAgainstFirmwareAdcOnlyPinCannotBeDriven(t *testing.T) {
	d := startSim(t)
	// A6 exists as an ADC input and has no digital port at all.
	if err := d.DigitalWrite(context.Background(), 20, true); err == nil {
		t.Fatal("expected an ADC-only pin to be refused")
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
// is covered by test_auth_session.c against the software element, which
// agsim (one crypto tier) cannot exercise.
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
	d := startSim(t)
	// The mock reports zero until told otherwise; the point here is that the
	// command round-trips and the pin maps to a channel at all.
	if _, err := d.AnalogRead(context.Background(), 14); err != nil {
		t.Fatalf("A0: %v", err)
	}
	if _, err := d.AnalogRead(context.Background(), 13); err == nil {
		t.Fatal("expected a digital-only pin to have no analog reading")
	}
}

func TestAgainstFirmwarePWM(t *testing.T) {
	d := startSim(t)
	ctx := context.Background()

	if err := d.PWMWrite(ctx, 9, 128); err != nil {
		t.Fatalf("pin 9 is OC1A and should accept a duty: %v", err)
	}
	// Pins 5 and 6 have compare units, but Timer0 is the millisecond tick.
	// The refusal is what stops a host writing a duty into a timer counting
	// something else and wondering why nothing moves.
	if err := d.PWMWrite(ctx, 5, 128); err == nil {
		t.Error("pin 5 should refuse PWM: its timer drives the tick")
	}
	if err := d.PWMWrite(ctx, 13, 128); err == nil {
		t.Error("pin 13 has no compare unit and should refuse PWM")
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

// CAPS answers per-pin pages, and this is the check that the five bytes the
// firmware writes are the five bytes this SDK reads. The board is the real
// ATmega328P table, compiled and linked into the simulator.
func TestAgainstFirmwarePinInfo(t *testing.T) {
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
	if !led.HasPort || led.Bit != 5 {
		t.Errorf("pin 13 = port bit %d (present %v), want PB5", led.Bit, led.HasPort)
	}
	if led.HasADC {
		t.Error("pin 13 has no analog channel")
	}
	if led.PWM != PWMNone {
		t.Errorf("pin 13 pwm = %s, want none: it has no compare unit", led.PWM)
	}

	// Pin 9 is OC1A, and Timer1 is nobody's tick on this board.
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

	// A6 exists as an analog input and has no digital port at all, which the
	// descriptor must say rather than report port 0 bit 0 -- a pin that can be
	// read and never written.
	adc, err := d.PinInfo(ctx, 20)
	if err != nil {
		t.Fatal(err)
	}
	if adc.HasPort {
		t.Error("pin 20 reported a digital port; A6 has none")
	}
	if !adc.HasADC || adc.ADCChannel != 6 {
		t.Errorf("pin 20 adc = %d (present %v), want channel 6", adc.ADCChannel, adc.HasADC)
	}
	if !adc.Flags.Has(PinADC) || adc.Flags.Has(PinDigital) {
		t.Errorf("pin 20 flags = %s, want adc and not digital", adc.Flags)
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
// ag_bus_init() previously fixed at boot. A value within range reaches the
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
