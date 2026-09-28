// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// These tests run against agsim -- the firmware compiled for the host -- and
// not against a fake. A fake would answer whatever this file told it to, which
// would prove that agctl talks to this file.
//
// What they pin is the rule the verbs exist for: a command that exists on the
// wire is reachable from agctl. A verb that stops being dispatched fails here
// with "unknown command", which is the regression that would otherwise be
// invisible -- the SDK method keeps working and only the CLI loses it.

package main

import (
	"bytes"
	"context"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	ag "github.com/jeremyhahn/arduino-generic/sdk/go"
)

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

func startSim(t *testing.T) *ag.Device {
	t.Helper()
	bin, err := filepath.Abs("../../../../build/host/agsim")
	if err != nil {
		t.Fatalf("resolving agsim path: %v", err)
	}
	// A failure, never a skip: a skipped cross-implementation test reports
	// success while checking nothing.
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
	return ag.New(c, ag.WithTimeout(5*time.Second))
}

// Every verb, with arguments the device will accept as well formed. Some
// answer with a protocol error -- a silent line times out, an absent element
// reports unsupported -- and that is a real answer from the firmware. What
// must never happen is agctl failing to dispatch.
func TestEveryPeripheralVerbIsDispatched(t *testing.T) {
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"analog", []string{"14"}},
		{"adc-ref", []string{"internal"}},
		{"pwm", []string{"3", "128"}},
		{"tone", []string{"11", "1000"}},
		{"notone", []string{"11"}},
		{"pulse-in", []string{"2", "1", "10ms"}},
		{"pulse-capture", []string{"2", "1", "8", "10ms"}},
		{"eeprom-read", []string{"0x080", "8"}},
		{"eeprom-write", []string{"0x080", "deadbeef"}},
		{"i2c", []string{"0x20", "01", "2"}},
		{"i2c", []string{"0x20", "-", "2"}},
		{"i2c-config", []string{"100000"}},
		{"spi", []string{"10", "0102"}},
		{"spi-config", []string{"0", "1", "msb"}},
		{"1wire-reset", []string{"4"}},
		{"1wire-write", []string{"4", "cc"}},
		{"1wire-read", []string{"4", "9"}},
		{"attest", []string{strings.Repeat("00", ag.AttestNonceLen)}},
		{"attest-cert", []string{strings.Repeat("00", ag.AliasNonceLen)}},
	} {
		t.Run(tc.verb+" "+strings.Join(tc.args, "_"), func(t *testing.T) {
			dev := startSim(t)
			var buf bytes.Buffer
			err := runPeripheral(context.Background(), dev, tc.verb, tc.args, false, &buf)
			if err != nil && strings.Contains(err.Error(), "unknown command") {
				t.Fatalf("%s is not dispatched: %v", tc.verb, err)
			}
			if err != nil && strings.HasPrefix(err.Error(), "usage:") {
				t.Fatalf("%s rejected well-formed arguments: %v", tc.verb, err)
			}
			var st ag.Status
			if err != nil && !errorsAsStatus(err, &st) {
				t.Fatalf("%s: %v", tc.verb, err)
			}
			if err == nil && buf.Len() == 0 {
				t.Fatalf("%s succeeded and printed nothing", tc.verb)
			}
		})
	}
}

// The answers agsim's mock HAL makes deterministic. These are the ones where
// "it was dispatched" is not enough and the VALUE matters.
func TestPeripheralVerbsReportWhatTheDeviceSaid(t *testing.T) {
	dev := startSim(t)

	t.Run("an unwritten EEPROM byte reads as erased", func(t *testing.T) {
		var buf bytes.Buffer
		if err := runPeripheral(context.Background(), dev, "eeprom-read", []string{"0x100", "4"}, false, &buf); err != nil {
			t.Fatal(err)
		}
		if got := strings.TrimSpace(buf.String()); got != "ffffffff" {
			t.Fatalf("erased EEPROM read %q, want ffffffff", got)
		}
	})

	t.Run("a write is read back", func(t *testing.T) {
		ctx := context.Background()
		if err := runPeripheral(ctx, dev, "eeprom-write", []string{"0x120", "a1b2c3"}, false, io.Discard); err != nil {
			t.Fatal(err)
		}
		var buf bytes.Buffer
		if err := runPeripheral(ctx, dev, "eeprom-read", []string{"0x120", "3"}, false, &buf); err != nil {
			t.Fatal(err)
		}
		if got := strings.TrimSpace(buf.String()); got != "a1b2c3" {
			t.Fatalf("read back %q, want a1b2c3", got)
		}
	})

	t.Run("a bus with nothing on it reports no device", func(t *testing.T) {
		var buf bytes.Buffer
		if err := runPeripheral(context.Background(), dev, "1wire-reset", []string{"4"}, false, &buf); err != nil {
			t.Fatal(err)
		}
		if got := strings.TrimSpace(buf.String()); got != "no device" {
			t.Fatalf("1wire-reset said %q, want \"no device\"", got)
		}
	})

	t.Run("an ADC pin converts and a digital-only pin is refused", func(t *testing.T) {
		var buf bytes.Buffer
		if err := runPeripheral(context.Background(), dev, "analog", []string{"14"}, false, &buf); err != nil {
			t.Fatalf("A0 (pin 14) should convert: %v", err)
		}
		if strings.TrimSpace(buf.String()) == "" {
			t.Fatal("analog printed nothing")
		}
		// Pin 2 has no ADC channel. The refusal is the board descriptor doing
		// its job, and it must reach the caller rather than reading as zero.
		if err := runPeripheral(context.Background(), dev, "analog", []string{"2"}, false, io.Discard); err == nil {
			t.Fatal("a digital-only pin was accepted for a conversion")
		}
	})

	t.Run("tone reaches the one pin the board wires to a timer", func(t *testing.T) {
		ctx := context.Background()
		if err := runPeripheral(ctx, dev, "tone", []string{"11", "1000"}, false, io.Discard); err != nil {
			t.Fatalf("the board's tone pin was refused: %v", err)
		}
		if err := runPeripheral(ctx, dev, "notone", []string{"11"}, false, io.Discard); err != nil {
			t.Fatal(err)
		}
		// Any other pin is refused rather than answered OK: a host whose
		// speaker is on the wrong leg has no other way to find out.
		if err := runPeripheral(ctx, dev, "tone", []string{"3", "1000"}, false, io.Discard); err == nil {
			t.Fatal("a pin the timer does not drive was accepted")
		}
		// Below the prescaler's floor the device refuses rather than playing
		// something else.
		if err := runPeripheral(ctx, dev, "tone", []string{"11", "440"}, false, io.Discard); err == nil {
			t.Fatal("a frequency below the floor was accepted")
		}
	})

	t.Run("json carries the same answer as text", func(t *testing.T) {
		var buf bytes.Buffer
		if err := runPeripheral(context.Background(), dev, "eeprom-read", []string{"0x100", "2"}, true, &buf); err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(buf.String(), `"hex": "ffff"`) {
			t.Fatalf("json output %q does not carry the bytes", buf.String())
		}
	})
}

// Bad input is refused by agctl, before a frame is put on the wire. The device
// is deliberately absent: any of these reaching it would hang or panic here
// rather than passing quietly.
func TestMalformedArgumentsAreRefusedBeforeTheWire(t *testing.T) {
	for _, tc := range []struct {
		name string
		verb string
		args []string
	}{
		{"analog without a pin", "analog", nil},
		{"analog with a non-numeric pin", "analog", []string{"gpio3"}},
		{"analog with a pin past a byte", "analog", []string{"300"}},
		{"adc-ref with an unknown reference", "adc-ref", []string{"bandgap"}},
		{"adc-ref with no reference", "adc-ref", nil},
		{"pwm without a duty", "pwm", []string{"3"}},
		{"pwm with a duty past a byte", "pwm", []string{"3", "256"}},
		{"tone with a frequency past a word", "tone", []string{"3", "70000"}},
		{"tone with no frequency", "tone", []string{"3"}},
		{"notone without a pin", "notone", nil},
		{"pulse-in with an unparseable timeout", "pulse-in", []string{"2", "1", "soon"}},
		{"pulse-in with too few arguments", "pulse-in", []string{"2", "1"}},
		{"pulse-capture with too few arguments", "pulse-capture", []string{"2", "1", "8"}},
		{"pulse-capture with an unparseable timeout", "pulse-capture", []string{"2", "1", "8", "ages"}},
		{"eeprom-read without a count", "eeprom-read", []string{"0x80"}},
		{"eeprom-write with odd-length hex", "eeprom-write", []string{"0x80", "abc"}},
		{"eeprom-write with empty hex", "eeprom-write", []string{"0x80", ""}},
		{"i2c with too few arguments", "i2c", []string{"0x20", "01"}},
		{"i2c with non-hex payload", "i2c", []string{"0x20", "zz", "1"}},
		{"spi without a payload", "spi", []string{"10"}},
		{"1wire-read without a count", "1wire-read", []string{"4"}},
		{"1wire-write with non-hex payload", "1wire-write", []string{"4", "gg"}},
		{"attest with a short nonce", "attest", []string{"0011"}},
		{"attest with no nonce", "attest", nil},
		{"attest-cert with a runtime-length nonce", "attest-cert", []string{strings.Repeat("00", ag.AttestNonceLen)}},
		{"a verb that does not exist", "morse", []string{"3"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := runPeripheral(context.Background(), nil, tc.verb, tc.args, false, io.Discard)
			if err == nil {
				t.Fatalf("%s %v was accepted", tc.verb, tc.args)
			}
		})
	}
}

// errorsAsStatus reports whether err is the device's own answer rather than a
// fault in agctl. A protocol status means the frame went out and came back.
func errorsAsStatus(err error, out *ag.Status) bool {
	for e := err; e != nil; {
		if s, ok := e.(ag.Status); ok {
			*out = s
			return true
		}
		u, ok := e.(interface{ Unwrap() error })
		if !ok {
			return false
		}
		e = u.Unwrap()
	}
	return false
}
