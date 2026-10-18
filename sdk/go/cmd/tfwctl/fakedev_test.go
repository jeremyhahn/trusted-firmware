// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// answerFunc is a fake device's reply to one request.
type answerFunc func(tfw.Frame) (tfw.Status, []byte)

// serveFrames decodes requests from r and writes answer's reply to each on w
// until r ends. first, when set, goes out ahead of the first reply, as an
// unsolicited event would.
func serveFrames(t testing.TB, r io.Reader, w io.Writer, answer answerFunc, first []byte) {
	var buf []byte
	chunk := make([]byte, 512)
	for {
		n, err := r.Read(chunk)
		if err != nil {
			return
		}
		buf = append(buf, chunk[:n]...)
		for {
			f, used, err := tfw.Decode(buf)
			if errors.Is(err, tfw.ErrShortFrame) {
				break
			}
			if err != nil {
				t.Errorf("fake device: undecodable request: %v", err)
				return
			}
			buf = buf[used:]
			st, payload := answer(f)
			reply, err := tfw.EncodeResponse(f.Seq, f.Cmd(), st, payload)
			if err != nil {
				t.Errorf("fake device: encoding the reply: %v", err)
				return
			}
			reply = append(first, reply...)
			first = nil
			if _, err := w.Write(reply); err != nil {
				return
			}
		}
	}
}

// startFakeDevice wires answer to a tfw.Device over two pipes.
func startFakeDevice(t *testing.T, answer answerFunc) *tfw.Device {
	t.Helper()
	reqR, reqW := io.Pipe()
	repR, repW := io.Pipe()
	go func() {
		// A fake that stops ends the link, so the Device fails at once
		// rather than waiting on a pipe that has no deadline.
		serveFrames(t, reqR, repW, answer, nil)
		_ = repW.Close()
	}()
	t.Cleanup(func() {
		_ = reqW.Close()
		_ = repW.Close()
	})
	conn := struct {
		io.Reader
		io.Writer
	}{repR, reqW}
	return tfw.New(conn, tfw.WithTimeout(5*time.Second), tfw.WithBootWait(0))
}

// bootEvent is the frame a device sends when it has just started.
func bootEvent(t testing.TB) []byte {
	t.Helper()
	b, err := tfw.EncodeResponse(tfw.SeqNone, tfw.Cmd(0xFF), tfw.StatusOK, []byte{tfw.EventBoot})
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// benchDevice answers every command tfwctl sends with a reply the SDK
// decodes, so each verb runs through to its output. refuse names commands it
// answers with a status instead.
type benchDevice struct {
	noFeatures bool
	refuse     map[tfw.Cmd]tfw.Status
}

func fill(n int, b byte) []byte { return bytes.Repeat([]byte{b}, n) }

func (b *benchDevice) answer(f tfw.Frame) (tfw.Status, []byte) {
	if st, ok := b.refuse[f.Cmd()]; ok {
		return st, nil
	}
	p := f.Payload
	switch f.Cmd() {
	case tfw.CmdPing:
		return tfw.StatusOK, p
	case tfw.CmdCaps:
		if len(p) == 1 {
			// Pin 14 is an analog-only input with no port; the rest are
			// port pins with no converter.
			if p[0] == 14 {
				return tfw.StatusOK, []byte{14, 0, 0xFF, 0xFF, 6, 0}
			}
			return tfw.StatusOK, []byte{p[0], 0, 2, 5, 0xFF, 1}
		}
		features := uint32(0xFFFFFFFF)
		if b.noFeatures {
			features = 0
		}
		c := []byte{tfw.ProtoVersionMajor, 0, 1, 2, 3, 0x01, 20, 4, 64, 1}
		c = binary.LittleEndian.AppendUint32(c, features)
		return tfw.StatusOK, binary.LittleEndian.AppendUint16(c, 1)
	case tfw.CmdSysInfo:
		s := binary.LittleEndian.AppendUint32(nil, 61000)
		s = append(s, 0x00, 0x04, 1, 1, 0, 2, 0)
		return tfw.StatusOK, binary.LittleEndian.AppendUint32(s, 2048)
	case tfw.CmdAppInfo:
		// Running, 7 overruns, ABI 1, 2 OTA slots, version 1.2.3, 16 config
		// bytes, needing CAPS bits 0 and 2; the name and profile NUL padded.
		a := []byte{byte(tfw.AppRunning), 7, 0, 1, 2, 1, 2, 3, 0, 16, 0, 5, 0, 0, 0}
		a = append(a, append([]byte("heartbeat"), make([]byte, 16-len("heartbeat"))...)...)
		return tfw.StatusOK, append(a, append([]byte("bench"), make([]byte, 32-len("bench"))...)...)
	case tfw.CmdDigitalRead, tfw.CmdOneWireReset:
		// Odd pins read high and have a device on the wire.
		return tfw.StatusOK, []byte{p[0] & 1}
	case tfw.CmdTimerSet:
		return tfw.StatusOK, []byte{2}
	case tfw.CmdTimerQuery:
		if p[0] == 0 {
			return tfw.StatusOK, make([]byte, 6)
		}
		return tfw.StatusOK, binary.LittleEndian.AppendUint32([]byte{1, 1}, 1500)
	case tfw.CmdAnalogRead:
		return tfw.StatusOK, []byte{0x34, 0x02}
	case tfw.CmdPulseIn:
		return tfw.StatusOK, binary.LittleEndian.AppendUint32(nil, 250)
	case tfw.CmdPulseCapture:
		return tfw.StatusOK, []byte{2, 10, 0, 20, 0}
	case tfw.CmdEEPROMRead, tfw.CmdI2CXfer, tfw.CmdSPIXfer, tfw.CmdOneWireRead:
		return tfw.StatusOK, []byte{0xDE, 0xAD}
	case tfw.CmdAttest:
		page := byte(0)
		if len(p) == 1 {
			page = p[0]
		}
		return tfw.StatusOK, append([]byte{page, tfw.EvidenceLen}, fill(60, 0xE0)...)
	case tfw.CmdAttestCert:
		page := byte(0)
		if len(p) == 1 {
			page = p[0]
		}
		return tfw.StatusOK, append([]byte{page, 0x2C, 0x01}, fill(60, 0x30)...)
	case tfw.CmdAttestCSR:
		return tfw.StatusOK, fill(64, 0x5A)
	case tfw.CmdAuthChallenge:
		return tfw.StatusOK, fill(tfw.AuthNonceLen, 0x11)
	case tfw.CmdOTAStatus:
		return tfw.StatusOK, binary.LittleEndian.AppendUint32([]byte{byte(tfw.OTAComplete)}, 4096)
	case tfw.CmdOTAInfo:
		i := []byte{2, 0, tfw.SlotTrial, 7, 0, 0, 0, 4, 3, 0, 0, 0}
		return tfw.StatusOK, append(i, fill(32, 0xAB)...)
	}
	// Everything else is a setter: OK and no payload.
	return tfw.StatusOK, nil
}

// runVerb runs one verb against a bench device and returns its output.
func runVerb(t *testing.T, b *benchDevice, asJSON bool, args ...string) (string, error) {
	t.Helper()
	dev := startFakeDevice(t, b.answer)
	var out bytes.Buffer
	err := run(context.Background(), dev, args[0], args[1:], asJSON, &out)
	return out.String(), err
}

var (
	nonce16 = strings.Repeat("ab", tfw.AttestNonceLen)
	nonce32 = strings.Repeat("cd", tfw.AliasNonceLen)
	otaSig  = "--sig=" + strings.Repeat("ee", tfw.OTAAuthSigLen)
)

// Every verb prints what the device answered, as text and as JSON.
func TestEveryVerbPrintsTheDevicesAnswer(t *testing.T) {
	key := writeTestECKey(t)
	for _, tc := range []struct {
		args []string
		text string
	}{
		{[]string{"ping", "hello", "there"}, `pong: "hello there"`},
		{[]string{"caps"}, "features    gpio sched analog"},
		{[]string{"pin", "3"}, "port.bit    2.5"},
		{[]string{"pin", "14"}, "adc channel 6"},
		{[]string{"sys"}, "free RAM 2048 bytes"},
		{[]string{"app"}, "name heartbeat\nprofile bench\nstate running\nversion 1.2.3\nabi 1\n" +
			"required caps 0x00000005\noverruns 7\nota slots 2\nconfig 16 bytes\n"},
		{[]string{"reset"}, "reset accepted"},
		{[]string{"reset", "boot"}, "reset accepted"},
		{[]string{"mode", "3", "in"}, "pin 3 -> in"},
		{[]string{"mode", "3", "out"}, "pin 3 -> out"},
		{[]string{"mode", "3", "pullup"}, "pin 3 -> pullup"},
		{[]string{"read", "3"}, "1\n"},
		{[]string{"read", "4"}, "0\n"},
		{[]string{"write", "3", "1"}, "pin 3 <- 1"},
		{[]string{"timer", "3", "1", "5s"}, "in slot 2"},
		{[]string{"timer-cancel", "3", "apply"}, "timer on pin 3 cancelled"},
		{[]string{"timer-query", "3"}, "held 1, 1.5s remaining"},
		{[]string{"timer-query", "0"}, "no timer on that pin"},
		{[]string{"ota-status"}, "4096 bytes received"},
		{[]string{"ota-info", "1"}, "image version 3"},
		{[]string{"ota-activate", otaSig}, "taken at the next reset"},
		{[]string{"ota-activate", otaSig, "now"}, "the device resets now"},
		{[]string{"ota-confirm", otaSig}, "settled at the next reset"},
		{[]string{"analog", "3"}, "564\n"},
		{[]string{"adc-ref", "vcc"}, "adc reference -> vcc"},
		{[]string{"adc-ref", "external"}, "adc reference -> external"},
		{[]string{"adc-ref", "internal"}, "adc reference -> internal"},
		{[]string{"pwm", "3", "128"}, "pin 3 <- duty 128"},
		{[]string{"tone", "3", "440"}, "pin 3 <- 440 Hz"},
		{[]string{"notone", "3"}, "pin 3 tone off"},
		{[]string{"pulse-in", "3", "1", "50ms"}, "250 us"},
		{[]string{"pulse-capture", "3", "1", "4", "50ms"}, "10 20\n"},
		{[]string{"eeprom-read", "0x10", "2"}, "dead\n"},
		{[]string{"eeprom-write", "0x10", "dead"}, "wrote 2 bytes at 0x010"},
		{[]string{"i2c", "0x50", "-", "2"}, "dead\n"},
		{[]string{"spi", "10", "beef"}, "dead\n"},
		{[]string{"i2c-config", "400000"}, "i2c clock -> 400000 Hz"},
		{[]string{"spi-config", "0", "1", "lsb"}, "lsb first"},
		{[]string{"spi-config", "3", "0", "msb"}, "msb first"},
		{[]string{"1wire-reset", "3"}, "presence"},
		{[]string{"1wire-reset", "4"}, "no device"},
		{[]string{"1wire-write", "3", "cc44"}, "wrote 2 bytes"},
		{[]string{"1wire-read", "3", "2"}, "dead\n"},
		{[]string{"attest", nonce16}, strings.Repeat("e0", tfw.EvidenceLen)},
		{[]string{"attest-cert", nonce32}, strings.Repeat("30", 300)},
		{[]string{"attest-csr", "--nonce", nonce32}, strings.Repeat("5a", 64)},
		{[]string{"auth", nonce16, key}, "authenticated"},
		{[]string{"auth", nonce16, key, "--", "read", "3"}, "1\n"},
	} {
		t.Run(strings.Join(tc.args[:1], " ")+"/text", func(t *testing.T) {
			got, err := runVerb(t, &benchDevice{}, false, tc.args...)
			if err != nil {
				t.Fatalf("%v: %v", tc.args, err)
			}
			if !strings.Contains(got, tc.text) {
				t.Fatalf("%v printed %q, want %q in it", tc.args, got, tc.text)
			}
		})
		t.Run(strings.Join(tc.args[:1], " ")+"/json", func(t *testing.T) {
			got, err := runVerb(t, &benchDevice{}, true, tc.args...)
			if err != nil {
				t.Fatalf("%v: %v", tc.args, err)
			}
			if !json.Valid([]byte(got)) {
				t.Fatalf("%v printed %q, want JSON", tc.args, got)
			}
		})
	}
}

// A device that reports no features is printed as having none.
func TestCapsNamesNoFeaturesWhenThereAreNone(t *testing.T) {
	got, err := runVerb(t, &benchDevice{noFeatures: true}, false, "caps")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(got, "features    (none)") {
		t.Fatalf("caps printed %q, want features (none)", got)
	}
}

// The authorization and the command it authorizes are each the device's to
// refuse, and tfwctl returns the refusal rather than claiming success.
func TestOTAVerbsReturnTheDevicesRefusal(t *testing.T) {
	for _, tc := range []struct {
		verb string
		cmd  tfw.Cmd
	}{
		{"ota-activate", tfw.CmdOTAAuth},
		{"ota-activate", tfw.CmdOTAActivate},
		{"ota-confirm", tfw.CmdOTAAuth},
		{"ota-confirm", tfw.CmdOTAConfirm},
	} {
		b := &benchDevice{refuse: map[tfw.Cmd]tfw.Status{tc.cmd: tfw.StatusErrAuthRequired}}
		got, err := runVerb(t, b, false, tc.verb, otaSig)
		if !errors.Is(err, tfw.StatusErrAuthRequired) {
			t.Fatalf("%s with %v refused: err = %v, want the refusal", tc.verb, tc.cmd, err)
		}
		if got != "" {
			t.Fatalf("%s printed %q after a refusal", tc.verb, got)
		}
	}
}

// A signature file holds the 64 raw bytes or their hex, and is given once.
func TestTakeOTASigReadsAFileOfEitherForm(t *testing.T) {
	raw := fill(tfw.OTAAuthSigLen, 0xEE)
	dir := t.TempDir()
	rawPath := filepath.Join(dir, "sig.bin")
	hexPath := filepath.Join(dir, "sig.hex")
	if err := os.WriteFile(rawPath, raw, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(hexPath, []byte(strings.Repeat("ee", tfw.OTAAuthSigLen)+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	for _, p := range []string{rawPath, hexPath} {
		sig, rest, found, err := takeOTASig([]string{"now", "--sig-file=" + p})
		if err != nil || !found || !bytes.Equal(sig[:], raw) || len(rest) != 1 {
			t.Fatalf("%s: sig %x, rest %v, found %v, err %v", p, sig, rest, found, err)
		}
	}
	if _, _, _, err := takeOTASig([]string{"--sig-file=" + rawPath, otaSig}); err == nil || !strings.Contains(err.Error(), "once") {
		t.Fatalf("two authorizations: err = %v, want it refused", err)
	}
}

// callMain runs main in this process with args, stdout and stderr captured.
// Only paths that return are run here: fail() exits, and runMain covers it.
func callMain(t *testing.T, args ...string) (stdout, stderr string) {
	t.Helper()
	dir := t.TempDir()
	open := func(name string) *os.File {
		f, err := os.Create(filepath.Join(dir, name))
		if err != nil {
			t.Fatal(err)
		}
		return f
	}
	o, e := open("stdout"), open("stderr")
	oldArgs, oldFlags, oldOut, oldErr := os.Args, flag.CommandLine, os.Stdout, os.Stderr
	t.Cleanup(func() {
		os.Args, flag.CommandLine, os.Stdout, os.Stderr = oldArgs, oldFlags, oldOut, oldErr
	})
	os.Args = append([]string{"tfwctl"}, args...)
	flag.CommandLine = flag.NewFlagSet("tfwctl", flag.ContinueOnError)
	os.Stdout, os.Stderr = o, e
	main()
	os.Stdout, os.Stderr = oldOut, oldErr
	read := func(f *os.File) string {
		_ = f.Close()
		b, err := os.ReadFile(f.Name())
		if err != nil {
			t.Fatal(err)
		}
		return string(b)
	}
	return read(o), read(e)
}

// ports lists what the host can see and returns; it opens nothing.
func TestMainListsPorts(t *testing.T) {
	if _, stderr := callMain(t, "ports"); stderr != "" {
		t.Fatalf("ports wrote %q to stderr", stderr)
	}
}

// freePort returns a loopback address nothing is listening on.
func freePort(t *testing.T) string {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := l.Addr().String()
	if err := l.Close(); err != nil {
		t.Fatal(err)
	}
	return addr
}

// Over tcp:// main listens, and the device that dials in answers the verb. A
// boot event during the exchange is reported, since what the caller knew
// about the device is stale.
func TestMainRunsAVerbOnTheDeviceThatDialsIn(t *testing.T) {
	addr := freePort(t)
	dialed := make(chan error, 1)
	go func() {
		// main has not necessarily started listening: dial until it has, or
		// until the bound.
		deadline := time.Now().Add(10 * time.Second)
		for {
			c, err := net.DialTimeout("tcp", addr, time.Second)
			if err == nil {
				dialed <- nil
				serveFrames(t, c, c, (&benchDevice{}).answer, bootEvent(t))
				_ = c.Close()
				return
			}
			if time.Now().After(deadline) {
				dialed <- err
				return
			}
		}
	}()
	stdout, stderr := callMain(t, "-timeout=10s", "tcp://"+addr, "ping", "hi")
	if err := <-dialed; err != nil {
		t.Fatalf("the device never reached main: %v", err)
	}
	if !strings.Contains(stdout, `pong: "hi"`) {
		t.Fatalf("stdout %q, want the device's pong", stdout)
	}
	if !strings.Contains(stderr, "boot event") {
		t.Fatalf("stderr %q, want the boot event noted", stderr)
	}
}
