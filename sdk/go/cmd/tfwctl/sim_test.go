// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

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
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"errors"
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
	return startSimWith(t)
}

// startSimWith starts tfwsim with env added to this process's environment:
// the TFWSIM_* presets tools/tfwsim/main.c reads to make the mock HAL answer
// a peripheral it otherwise never does.
func startSimWith(t *testing.T, env ...string) *tfw.Device {
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
	cmd.Env = append(os.Environ(), env...)
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
			if tc.verb == "auth" && st != tfw.StatusErrUnknownCmd {
				t.Fatalf("auth on the soft tier: err = %v, want %v", err, tfw.StatusErrUnknownCmd)
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
		if err := runPeripheral(context.Background(), dev, "eeprom-read", []string{"0x180", "4"}, false, &buf); err != nil {
			t.Fatal(err)
		}
		if got := strings.TrimSpace(buf.String()); got != "ffffffff" {
			t.Fatalf("erased EEPROM read %q, want ffffffff", got)
		}
	})

	t.Run("a write is read back", func(t *testing.T) {
		ctx := context.Background()
		if err := runPeripheral(ctx, dev, "eeprom-write", []string{"0x1A0", "a1b2c3"}, false, io.Discard); err != nil {
			t.Fatal(err)
		}
		var buf bytes.Buffer
		if err := runPeripheral(ctx, dev, "eeprom-read", []string{"0x1A0", "3"}, false, &buf); err != nil {
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
		var st tfw.Status
		if err := runPeripheral(context.Background(), dev, "analog", []string{"2"}, false, io.Discard); !errorsAsStatus(err, &st) {
			t.Fatalf("a digital-only pin: err = %v, want the device's refusal", err)
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
		var st tfw.Status
		if err := runPeripheral(ctx, dev, "tone", []string{"3", "1000"}, false, io.Discard); !errorsAsStatus(err, &st) {
			t.Fatalf("a pin the timer does not drive: err = %v, want the device's refusal", err)
		}
		// Below the prescaler's floor the device refuses rather than playing
		// something else.
		if err := runPeripheral(ctx, dev, "tone", []string{"11", "440"}, false, io.Discard); !errorsAsStatus(err, &st) {
			t.Fatalf("a frequency below the floor: err = %v, want the device's refusal", err)
		}
	})

	t.Run("json carries the same answer as text", func(t *testing.T) {
		var buf bytes.Buffer
		if err := runPeripheral(context.Background(), dev, "eeprom-read", []string{"0x180", "2"}, true, &buf); err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(buf.String(), `"hex": "ffff"`) {
			t.Fatalf("json output %q does not carry the bytes", buf.String())
		}
	})
}

func TestPeripheralVerbsCoverEveryOption(t *testing.T) {
	dev := startSim(t)
	ctx := context.Background()
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"adc-ref", []string{"vcc"}},
		{"adc-ref", []string{"external"}},
		{"spi-config", []string{"0", "1", "lsb"}},
		{"spi-config", []string{"0", "1", "msb"}},
		{"mode", []string{"3", "in"}},
		{"mode", []string{"3", "pullup"}},
		{"timer-query", []string{"3"}},
	} {
		t.Run(tc.verb+" "+strings.Join(tc.args, "_"), func(t *testing.T) {
			for _, asJSON := range []bool{false, true} {
				var buf bytes.Buffer
				if err := run(ctx, dev, tc.verb, tc.args, asJSON, &buf); err != nil {
					t.Fatalf("%s %v: %v", tc.verb, tc.args, err)
				}
				if buf.Len() == 0 {
					t.Fatalf("%s %v printed nothing", tc.verb, tc.args)
				}
			}
		})
	}
}

func TestRunCoreVerbsAgainstTfwsim(t *testing.T) {
	dev := startSim(t)
	ctx := context.Background()
	for _, tc := range []struct {
		verb string
		args []string
		text string
	}{
		{"ping", []string{"hello", "there"}, `pong: "hello there"`},
		{"caps", nil, "protocol    "},
		{"pin", []string{"13"}, "pin         13"},
		{"sys", nil, "uptime "},
		{"mode", []string{"13", "out"}, "pin 13 -> out"},
		{"mode", []string{"12", "in"}, "pin 12 -> in"},
		{"mode", []string{"12", "pullup"}, "pin 12 -> pullup"},
		{"write", []string{"13", "1"}, "pin 13 <- 1"},
		{"read", []string{"12"}, ""},
		{"timer", []string{"13", "1", "5s"}, "pin 13 held for 5s in slot "},
		{"timer-query", []string{"13"}, "held 1, "},
		{"timer-cancel", []string{"13"}, "timer on pin 13 cancelled"},
		{"timer-query", []string{"13"}, "no timer on that pin"},
	} {
		var text, js bytes.Buffer
		if err := run(ctx, dev, tc.verb, tc.args, false, &text); err != nil {
			t.Fatalf("%s %v: %v", tc.verb, tc.args, err)
		}
		if !strings.HasPrefix(text.String(), tc.text) {
			t.Errorf("%s %v printed %q, want prefix %q", tc.verb, tc.args, text.String(), tc.text)
		}
		if tc.verb == "timer" || tc.verb == "timer-cancel" {
			continue // one-shot on the device; the JSON form is checked below
		}
		if err := run(ctx, dev, tc.verb, tc.args, true, &js); err != nil {
			t.Fatalf("%s %v --json: %v", tc.verb, tc.args, err)
		}
		if !json.Valid(js.Bytes()) {
			t.Errorf("%s %v --json printed %q, not JSON", tc.verb, tc.args, js.String())
		}
	}
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"timer", []string{"13", "1", "5s"}},
		{"timer-cancel", []string{"13"}},
	} {
		var js bytes.Buffer
		if err := run(ctx, dev, tc.verb, tc.args, true, &js); err != nil {
			t.Fatalf("%s %v --json: %v", tc.verb, tc.args, err)
		}
		if !json.Valid(js.Bytes()) {
			t.Errorf("%s %v --json printed %q, not JSON", tc.verb, tc.args, js.String())
		}
	}
}

func TestRunDeviceRefusalsAreTheDevicesStatus(t *testing.T) {
	dev := startSim(t)
	ctx := context.Background()
	for _, tc := range []struct {
		verb string
		args []string
	}{
		{"pin", []string{"200"}},
		{"mode", []string{"200", "out"}},
		{"read", []string{"200"}},
		{"write", []string{"200", "1"}},
		{"timer", []string{"200", "1", "5s"}},
		{"timer-cancel", []string{"200"}},
		// timer-query is absent on purpose: the firmware answers a pin it
		// does not have with "no timer", which is true.
	} {
		err := run(ctx, dev, tc.verb, tc.args, false, io.Discard)
		var st tfw.Status
		if !errors.As(err, &st) {
			t.Errorf("%s %v: err = %v, want the device's status", tc.verb, tc.args, err)
		}
	}
}

// simImage is an image the firmware's stager accepts: whole pages, the body,
// then the v1 trailer naming the body's length and SHA-256.
func simImage(total, body int) []byte {
	img := bytes.Repeat([]byte{0xFF}, total)
	copy(img, patterned(body))
	tr := img[total-128:]
	clear(tr)
	copy(tr, "TFIM")
	binary.LittleEndian.PutUint16(tr[4:], 1)
	tr[6] = 0x01
	binary.LittleEndian.PutUint32(tr[8:], 3)
	binary.LittleEndian.PutUint32(tr[12:], uint32(body))
	sum := sha256.Sum256(img[:body])
	copy(tr[16:], sum[:])
	return img
}

// Against the firmware: tfwsim links the application stager and a store
// (mk/host.mk, TFWSIM_STAGE_SRCS), so the verb stages a real image and the
// device's own gate refuses a tampered one at END.
func TestOTAStageAgainstFirmware(t *testing.T) {
	dev := startSim(t)
	ctx := context.Background()

	var buf bytes.Buffer
	if err := run(ctx, dev, "ota-stage", []string{writeImage(t, simImage(1024, 700))}, false, &buf); err != nil {
		t.Fatalf("ota-stage: %v", err)
	}
	if got := strings.TrimSpace(buf.String()); got != tfw.OTAComplete.String()+", 1024 bytes received" {
		t.Fatalf("ota-stage printed %q", got)
	}
	expectSimStatus(t, dev, tfw.OTAComplete.String()+", 1024 bytes received")

	img := simImage(1024, 700)
	img[100] ^= 0x40
	err := run(ctx, dev, "ota-stage", []string{writeImage(t, img)}, false, io.Discard)
	if !errors.Is(err, tfw.StatusErrCrypto) {
		t.Fatalf("tampered image: %v, want %v", err, tfw.StatusErrCrypto)
	}
	// The stager's fail() (src/core/ota_stage.c) sets FAILED and leaves the
	// count alone; only OTA_BEGIN zeroes it.
	expectSimStatus(t, dev, tfw.OTAFailed.String()+", 1024 bytes received")
}
