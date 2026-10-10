// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// These tests run against tfwsim -- the firmware compiled for the host -- and
// not against a fake. A fake would answer whatever this file told it to, which
// would prove that tfwctl talks to this file.
//
// What they pin is the rule the verbs exist for: a command that exists on the
// wire is reachable from tfwctl. A verb that stops being dispatched fails here
// with "unknown command", which is the regression that would otherwise be
// invisible -- the SDK method keeps working and only the CLI loses it.

package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// A throwaway operator key, PEM-encoded to a temp file -- exactly the shape
// loadECPrivateKey reads, and the only kind of "key material" this test (or
// tfwctl itself) ever holds: one generated for the duration of one test run,
// never a real device's or fleet's.
func writeTestECKey(t *testing.T) string {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("generating test key: %v", err)
	}
	der, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatalf("marshalling test key: %v", err)
	}
	path := filepath.Join(t.TempDir(), "operator.pem")
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("writing test key: %v", err)
	}
	defer f.Close()
	if err := pem.Encode(f, &pem.Block{Type: "EC PRIVATE KEY", Bytes: der}); err != nil {
		t.Fatalf("encoding test key: %v", err)
	}
	return path
}

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

func startSim(t *testing.T) *tfw.Device {
	t.Helper()
	bin, err := filepath.Abs("../../../../build/host/tfwsim")
	if err != nil {
		t.Fatalf("resolving tfwsim path: %v", err)
	}
	// A failure, never a skip: a skipped cross-implementation test reports
	// success while checking nothing.
	if _, err := os.Stat(bin); err != nil {
		t.Fatalf("tfwsim not built at %s -- run `make tfwsim` first: %v", bin, err)
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
	return tfw.New(c, tfw.WithTimeout(5*time.Second))
}

// Every verb, with arguments the device will accept as well formed. Some
// answer with a protocol error -- a silent line times out, an absent element
// reports unsupported -- and that is a real answer from the firmware. What
// must never happen is tfwctl failing to dispatch.
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
		{"attest", []string{strings.Repeat("00", tfw.AttestNonceLen)}},
		{"attest-cert", []string{strings.Repeat("00", tfw.AliasNonceLen)}},
		{"attest-csr", []string{"--nonce", strings.Repeat("00", tfw.AliasNonceLen)}},
		// tfwsim links the soft tier, so this reaches TFW_ERR_UNKNOWN_CMD --
		// a real Status, not a dispatch failure, which is exactly what this
		// table checks: the verb itself is wired up in tfwctl regardless of
		// what the connected device's tier can do with it.
		{"auth", []string{strings.Repeat("00", tfw.SerialLen), "$KEY"}},
	} {
		t.Run(tc.verb+" "+strings.Join(tc.args, "_"), func(t *testing.T) {
			dev := startSim(t)
			args := tc.args
			for i, a := range args {
				if a == "$KEY" {
					args = append([]string(nil), args...)
					args[i] = writeTestECKey(t)
				}
			}
			var buf bytes.Buffer
			err := runPeripheral(context.Background(), dev, tc.verb, args, false, &buf)
			// Quoted and naming THIS verb: runPeripheral's own dispatch
			// failure, `fmt.Errorf("unknown command %q", cmd)`. A bare,
			// unquoted "unknown command" is StatusErrUnknownCmd's own
			// String() -- the DEVICE answering, correctly, that this
			// build does not implement the id (auth on the soft tier is
			// exactly that case) -- and must not be confused with tfwctl
			// failing to dispatch the verb at all.
			if err != nil && strings.Contains(err.Error(), fmt.Sprintf("unknown command %q", tc.verb)) {
				t.Fatalf("%s is not dispatched: %v", tc.verb, err)
			}
			if err != nil && strings.HasPrefix(err.Error(), "usage:") {
				t.Fatalf("%s rejected well-formed arguments: %v", tc.verb, err)
			}
			var st tfw.Status
			if err != nil && !errorsAsStatus(err, &st) {
				t.Fatalf("%s: %v", tc.verb, err)
			}
			if err == nil && buf.Len() == 0 {
				t.Fatalf("%s succeeded and printed nothing", tc.verb)
			}
		})
	}
}

// The answers tfwsim's mock HAL makes deterministic. These are the ones where
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

// Bad input is refused by tfwctl, before a frame is put on the wire. The device
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
		{"attest-cert with a runtime-length nonce", "attest-cert", []string{strings.Repeat("00", tfw.AttestNonceLen)}},
		{"auth with too few arguments", "auth", []string{strings.Repeat("00", tfw.SerialLen)}},
		{"auth with a short serial", "auth", []string{"0011", "/dev/null"}},
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
// fault in tfwctl. A protocol status means the frame went out and came back.
func errorsAsStatus(err error, out *tfw.Status) bool {
	for e := err; e != nil; {
		if s, ok := e.(tfw.Status); ok {
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
