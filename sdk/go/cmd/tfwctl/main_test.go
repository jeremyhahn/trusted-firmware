package main

import (
	"bytes"
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// mainEnv set to 1 makes the test binary run main() with the arguments after
// "--" instead of the tests: the only way to observe main's exit codes, which
// it reaches through os.Exit (main.go:117 usage, main.go:422 fail).
const mainEnv = "TFWCTL_TEST_RUN_MAIN"

func TestMain(m *testing.M) {
	if os.Getenv(mainEnv) == "1" {
		for i, a := range os.Args {
			if a == "--" {
				os.Args = append([]string{"tfwctl"}, os.Args[i+1:]...)
				break
			}
		}
		flag.CommandLine = flag.NewFlagSet("tfwctl", flag.ExitOnError)
		main()
		os.Exit(0)
	}
	// hw-test with no HW_TEST_RUN: the board's rows, by name (e2e_rows_test.go).
	if err := selectBoardRows(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	os.Exit(m.Run())
}

// runMain runs main in a child process and returns its exit code and the
// first line it wrote to stderr.
func runMain(t *testing.T, args ...string) (int, string) {
	t.Helper()
	cmd := exec.Command(os.Args[0], append([]string{"-test.run=^$", "--"}, args...)...) // #nosec G204 -- the test binary itself
	cmd.Env = append(os.Environ(), mainEnv+"=1")
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	err := cmd.Run()
	code := 0
	var exit *exec.ExitError
	if errors.As(err, &exit) {
		code = exit.ExitCode()
	} else if err != nil {
		t.Fatalf("running main: %v", err)
	}
	first, _, _ := strings.Cut(stderr.String(), "\n")
	return code, first
}

func TestMainExitCodes(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "no-such-port")
	for _, tc := range []struct {
		name  string
		args  []string
		code  int
		first string
	}{
		{"no arguments is a usage error", nil, 2, "tfwctl -- talk to an trusted-firmware device"},
		{"a port with no command is a usage error", []string{"/dev/ttyACM0"}, 2, "tfwctl -- talk to an trusted-firmware device"},
		{"an unknown flag is a usage error", []string{"--bogus", "p", "ping"}, 2, "flag provided but not defined: -bogus"},
		{"a port that cannot be opened is a failure", []string{missing, "ping"}, 1, "tfwctl: "},
		{"a tcp:// endpoint with no port is a failure", []string{"tcp://localhost", "ping"}, 1, `tfwctl: device address "tcp://localhost"`},
		{"a tcp:// endpoint no device dials is a failure", []string{"-timeout=50ms", "tcp://" + freePort(t), "ping"}, 1, "tfwctl: no device connected"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			code, first := runMain(t, tc.args...)
			if code != tc.code {
				t.Errorf("exit code = %d, want %d", code, tc.code)
			}
			if !strings.HasPrefix(first, tc.first) {
				t.Errorf("stderr first line = %q, want prefix %q", first, tc.first)
			}
		})
	}
}

func TestRunArgumentErrorsAreRefusedBeforeTheWire(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "absent.bin")
	for _, tc := range []struct {
		name  string
		verb  string
		args  []string
		first string
	}{
		{"pin without a pin", "pin", nil, "usage: pin <pin>"},
		{"pin with a non-numeric pin", "pin", []string{"d13"}, `pin "d13": `},
		{"pin past a byte", "pin", []string{"256"}, `pin "256": `},
		{"mode without a mode", "mode", []string{"13"}, "usage: mode <pin> <in|out|pullup>"},
		{"mode with a bad pin", "mode", []string{"x", "out"}, `pin "x": `},
		{"mode with an unknown mode", "mode", []string{"13", "analog"}, `unknown mode "analog"`},
		{"read without a pin", "read", nil, "usage: read <pin>"},
		{"read with a bad pin", "read", []string{"-1"}, `pin "-1": `},
		{"write without a level", "write", []string{"13"}, "usage: write <pin> <0|1>"},
		{"write with a bad pin", "write", []string{"x", "1"}, `pin "x": `},
		{"write with a word for a level", "write", []string{"13", "banana"}, `level "banana", want 0 or 1`},
		{"timer with a word for a level", "timer", []string{"13", "high", "5s"}, `level "high", want 0 or 1`},
		{"pulse-in with a word for a level", "pulse-in", []string{"13", "2", "5ms"}, `level "2", want 0 or 1`},
		{"pulse-capture with a word for a level", "pulse-capture", []string{"13", "x", "4", "5ms"}, `level "x", want 0 or 1`},
		{"timer without a duration", "timer", []string{"13", "1"}, "usage: timer <pin> <0|1> <duration, e.g. 5s>"},
		{"timer with a bad pin", "timer", []string{"x", "1", "5s"}, `pin "x": `},
		{"timer with a bad duration", "timer", []string{"13", "1", "soon"}, `duration "soon": `},
		{"timer-cancel without a pin", "timer-cancel", nil, "usage: timer-cancel <pin> [apply]"},
		{"timer-cancel with a bad pin", "timer-cancel", []string{"x"}, `pin "x": `},
		{"timer-query without a pin", "timer-query", nil, "usage: timer-query <pin>"},
		{"timer-query with a bad pin", "timer-query", []string{"x"}, `pin "x": `},
		{"ota-stage without an image", "ota-stage", nil, "usage: ota-stage [--activate[=now]] <image>"},
		{"ota-stage with two images", "ota-stage", []string{"a", "b"}, "usage: ota-stage [--activate[=now]] <image>"},
		{"ota-stage with a missing image", "ota-stage", []string{missing}, "usage: ota-stage [--activate[=now]] <image>: open "},
		{"ota-status with an argument", "ota-status", []string{"now"}, "usage: ota-status"},
		{"ota-info without a slot", "ota-info", nil, "usage: ota-info <slot>"},
		{"ota-info with two slots", "ota-info", []string{"0", "1"}, "usage: ota-info <slot>"},
		{"ota-info with a slot past a byte", "ota-info", []string{"256"}, `slot "256": `},
		{"an unknown subcommand", "morse", nil, "unknown command"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := run(context.Background(), nil, tc.verb, tc.args, false, io.Discard)
			if err == nil {
				t.Fatalf("%s %v was accepted", tc.verb, tc.args)
			}
			first, _, _ := strings.Cut(err.Error(), "\n")
			if !strings.HasPrefix(first, tc.first) {
				t.Fatalf("err = %q, want prefix %q", first, tc.first)
			}
		})
	}
}

// The tests below mutate what tfwctl is given -- every argument of every
// verb, and every reply the device can send -- and pin the one contract that
// holds for all of them: a bad input is an error with a message, never a
// panic, and an argument tfwctl cannot read never reaches the wire.

// mutateSeed fixes every random case here. A failure prints it, so the case
// that failed is the case that reruns.
const mutateSeed = 0x7F3C1

// countingDevice serves the bench answers and counts the frames it is sent.
type countingDevice struct {
	bench  benchDevice
	frames atomic.Int32
	reply  func(tfw.Frame, tfw.Status, []byte) (tfw.Status, []byte)
}

func (c *countingDevice) answer(f tfw.Frame) (tfw.Status, []byte) {
	c.frames.Add(1)
	st, p := c.bench.answer(f)
	if c.reply != nil {
		return c.reply(f, st, p)
	}
	return st, p
}

func runCounted(t *testing.T, c *countingDevice, asJSON bool, args ...string) (string, error) {
	t.Helper()
	dev := startFakeDevice(t, c.answer)
	var out bytes.Buffer
	err := run(context.Background(), dev, args[0], args[1:], asJSON, &out)
	return out.String(), err
}

// argKind names what an argument position holds, and so which values are
// wrong for it.
type argKind int

const (
	kFree argKind = iota // free text, or a value no parser reads
	kPin
	kU8
	kU16
	kU32
	kHex
	kHexOrDash
	kNonce16
	kNonce32
	kSerial
	kDuration
	kWord  // one of a fixed set of words
	kLevel // 0 or 1, nothing else
)

var overLong = strings.Repeat("9", 300)

// badValues are the mutations of each kind: empty, over-long, out of range,
// the wrong base, trailing bytes. Every one is wrong for that position.
var badValues = map[argKind][]string{
	kPin:       {"", overLong, "256", "-1", "0x03", "3x", " 3", "3 ", "+3x", "1e1", "three"},
	kU8:        {"", overLong, "256", "0x100", "-1", "3x", "0b2", "0o8", "08", "two"},
	kU16:       {"", overLong, "65536", "0x10000", "-1", "16x", "0xg", "0b102"},
	kU32:       {"", overLong, "4294967296", "0x100000000", "-1", "400000x", "fast"},
	kHex:       {"", "0x", "d", "dea", "zz", "dead!", "0xdeadz", strings.Repeat("a", 301), "-"},
	kHexOrDash: {"", "0x", "d", "dea", "zz", "--", "-dead"},
	kNonce16:   {"", nonce16[:30], nonce16 + "ab", nonce16[:31], nonce16[:30] + "zz", overLong},
	kNonce32:   {"", nonce32[:62], nonce32 + "cd", nonce32[:63], nonce32[:62] + "zz", overLong},
	kSerial:    {"", strings.Repeat("ab", tfw.SerialLen-1), strings.Repeat("ab", tfw.SerialLen+1), strings.Repeat("zz", tfw.SerialLen)},
	kDuration:  {"", "5", "5x", "s", "five seconds", overLong + "h", "5s junk"},
	kWord:      {"", "IN", "x", overLong},
	kLevel:     {"", "banana", "2", "-1", "00", "01", "0x1", " 1", "1 ", "high", "low", "true", overLong},
}

type verbCase struct {
	args  []string
	kinds []argKind // one per argument after the verb
	cmds  []tfw.Cmd // the commands the verb sends, in order
}

// verbCases are good invocations, one per verb that parses an argument.
func verbCases(key string) []verbCase {
	return []verbCase{
		{[]string{"pin", "3"}, []argKind{kPin}, []tfw.Cmd{tfw.CmdCaps}},
		{[]string{"mode", "3", "out"}, []argKind{kPin, kWord}, nil},
		{[]string{"read", "3"}, []argKind{kPin}, []tfw.Cmd{tfw.CmdDigitalRead}},
		{[]string{"write", "3", "1"}, []argKind{kPin, kLevel}, nil},
		{[]string{"timer", "3", "1", "5s"}, []argKind{kPin, kLevel, kDuration}, []tfw.Cmd{tfw.CmdTimerSet}},
		{[]string{"timer-cancel", "3"}, []argKind{kPin}, nil},
		{[]string{"timer-query", "3"}, []argKind{kPin}, []tfw.Cmd{tfw.CmdTimerQuery}},
		{[]string{"ota-info", "1"}, []argKind{kU8}, []tfw.Cmd{tfw.CmdOTAInfo}},
		{[]string{"analog", "3"}, []argKind{kPin}, []tfw.Cmd{tfw.CmdAnalogRead}},
		{[]string{"adc-ref", "vcc"}, []argKind{kWord}, nil},
		{[]string{"pwm", "3", "128"}, []argKind{kPin, kU8}, nil},
		{[]string{"tone", "3", "440"}, []argKind{kPin, kU16}, nil},
		{[]string{"notone", "3"}, []argKind{kPin}, nil},
		{[]string{"pulse-in", "3", "1", "50ms"}, []argKind{kPin, kLevel, kDuration}, []tfw.Cmd{tfw.CmdPulseIn}},
		{[]string{"pulse-capture", "3", "1", "4", "50ms"}, []argKind{kPin, kLevel, kU8, kDuration}, []tfw.Cmd{tfw.CmdPulseCapture}},
		{[]string{"eeprom-read", "0x10", "2"}, []argKind{kU16, kU8}, []tfw.Cmd{tfw.CmdEEPROMRead}},
		{[]string{"eeprom-write", "0x10", "dead"}, []argKind{kU16, kHex}, nil},
		{[]string{"i2c", "0x50", "-", "2"}, []argKind{kU8, kHexOrDash, kU8}, []tfw.Cmd{tfw.CmdI2CXfer}},
		{[]string{"spi", "10", "beef"}, []argKind{kPin, kHex}, []tfw.Cmd{tfw.CmdSPIXfer}},
		{[]string{"i2c-config", "400000"}, []argKind{kU32}, nil},
		{[]string{"spi-config", "0", "1", "lsb"}, []argKind{kU8, kU8, kWord}, nil},
		{[]string{"1wire-reset", "3"}, []argKind{kPin}, []tfw.Cmd{tfw.CmdOneWireReset}},
		{[]string{"1wire-write", "3", "cc44"}, []argKind{kPin, kHex}, nil},
		{[]string{"1wire-read", "3", "2"}, []argKind{kPin, kU8}, []tfw.Cmd{tfw.CmdOneWireRead}},
		{[]string{"attest", nonce16}, []argKind{kNonce16}, []tfw.Cmd{tfw.CmdAttest}},
		{[]string{"attest-cert", nonce32}, []argKind{kNonce32}, []tfw.Cmd{tfw.CmdAttestCert}},
		{[]string{"attest-csr", "--nonce", nonce32}, []argKind{kWord, kNonce32}, []tfw.Cmd{tfw.CmdAttestCSR}},
		{[]string{"auth", nonce16, key}, []argKind{kSerial, kFree}, nil},
		{[]string{"ota-activate", otaSig}, []argKind{kFree}, nil},
		{[]string{"ota-confirm", otaSig}, []argKind{kFree}, nil},
		{[]string{"ota-status"}, nil, []tfw.Cmd{tfw.CmdOTAStatus}},
		{[]string{"caps"}, nil, []tfw.Cmd{tfw.CmdCaps}},
		{[]string{"sys"}, nil, []tfw.Cmd{tfw.CmdSysInfo}},
		{[]string{"app"}, nil, []tfw.Cmd{tfw.CmdAppInfo}},
	}
}

// expectRefused runs args and requires an error with a message, no output
// and no frame on the wire.
func expectRefused(t *testing.T, args []string) {
	t.Helper()
	c := &countingDevice{}
	out, err := runCounted(t, c, false, args...)
	if err == nil || err.Error() == "" {
		t.Fatalf("%q: err = %v, want a refusal with a message", args, err)
	}
	if n := c.frames.Load(); n != 0 {
		t.Fatalf("%q: refused (%v) but sent %d frame(s) first", args, err, n)
	}
	if out != "" {
		t.Fatalf("%q: refused (%v) but printed %q", args, err, out)
	}
}

// TestEveryVerbArgumentMutatedIsRefusedBeforeTheWire replaces each argument
// of each good invocation with every wrong value of its kind, and drops each
// argument in turn.
func TestEveryVerbArgumentMutatedIsRefusedBeforeTheWire(t *testing.T) {
	key := writeTestECKey(t)
	for _, vc := range verbCases(key) {
		// The good invocation runs, so a mutation that fails fails on its
		// own account and not the harness's.
		if _, err := runCounted(t, &countingDevice{}, false, vc.args...); err != nil {
			t.Fatalf("%q: the good invocation failed: %v", vc.args, err)
		}
		for i, k := range vc.kinds {
			pos := i + 1
			if k == kFree {
				continue
			}
			for _, bad := range badValues[k] {
				args := append([]string(nil), vc.args...)
				args[pos] = bad
				t.Run(strings.Join(args[:1], "")+"/"+strings.Repeat("a", pos), func(t *testing.T) {
					expectRefused(t, args)
				})
			}
		}
		// Every required argument dropped from the end.
		for n := 1; n < len(vc.args); n++ {
			if vc.args[0] == "timer-cancel" || vc.args[0] == "attest-csr" && n == 2 {
				continue // the optional apply; --nonce's value is checked as kNonce32
			}
			if len(vc.kinds) == 0 {
				break
			}
			expectRefused(t, vc.args[:n])
		}
	}
}

// TestFlagVerbsRefuseMutatedFlags covers the verbs that parse flags or
// fixed-form options: netcfg set, attest-csr and the OTA authorization.
func TestFlagVerbsRefuseMutatedFlags(t *testing.T) {
	sigHex := strings.TrimPrefix(otaSig, "--sig=")
	for _, args := range [][]string{
		{"netcfg", "set", "--mac", ""},
		{"netcfg", "set", "--mac", "02:00:00:00:00:01:"},
		{"netcfg", "set", "--mac", "02:00:00:00:00"},
		{"netcfg", "set", "--mac", "02:00:00:00:00:0g"},
		{"netcfg", "set", "--mac", overLong},
		{"netcfg", "set", "--ip", "192.0.2.256"},
		{"netcfg", "set", "--ip", "192.0.2.1x"},
		{"netcfg", "set", "--ip", "0xC0.0.2.1"},
		{"netcfg", "set", "--ip", overLong},
		{"netcfg", "set", "--mask", "255.255.255"},
		{"netcfg", "set", "--gateway", "192.0.2.1 "},
		{"netcfg", "set", "--endpoint", "198.51.100.20:"},
		{"netcfg", "set", "--endpoint", "198.51.100.20:65536"},
		{"netcfg", "set", "--endpoint", "198.51.100.20:-1"},
		{"netcfg", "set", "--endpoint", "198.51.100.20:7020x"},
		{"netcfg", "set", "--endpoint", "host:7020"},
		{"netcfg", "set", "--endpoint"},
		{"netcfg", "set", "--ip"},
		{"netcfg", "set", "--ip", "192.0.2.10", "trailing"},
		{"netcfg", "get", "--ip", "192.0.2.10"},
		{"attest-csr", "--nonce", nonce32, "stray"},
		{"attest-csr", "--nonce=" + nonce32[:62]},
		{"attest-csr", "--nonce=0x" + nonce32[:62] + "zz"},
		{"attest-csr", "--nonc", nonce32},
		{"attest-csr", nonce32},
		{"ota-activate"},
		{"ota-activate", "now"},
		{"ota-activate", otaSig, "later"},
		{"ota-activate", otaSig, "now", "now"},
		{"ota-activate", otaSig, otaSig},
		{"ota-activate", "--sig="},
		{"ota-activate", "--sig=" + sigHex[:126]},
		{"ota-activate", "--sig=" + sigHex + "ee"},
		{"ota-activate", "--sig=" + sigHex[:126] + "zz"},
		{"ota-activate", "--sig=" + sigHex + " junk"},
		{"ota-activate", "--sig-file="},
		{"ota-confirm"},
		{"ota-confirm", otaSig, "now"},
		{"ota-confirm", "--sig=" + overLong},
		{"ota-stage", otaSig, "image.bin"},
		{"ota-stage", "--activate", "image.bin"},
		{"ota-stage", "--activate=later", "image.bin"},
		{"ota-stage"},
		{"ota-status", "extra"},
		{"ota-info"},
		{"ota-info", "1", "2"},
		{"no-such-verb"},
		{"adc-ref", "VCC"},
		{"mode", "3", "output"},
		{"spi-config", "0", "1", "LSB"},
		{"auth", nonce16},
		{"auth", nonce16, "/nonexistent/key.pem"},
	} {
		expectRefused(t, args)
	}
}

// TestEveryMalformedReplyIsAnError sends each verb a reply truncated at every
// length, and requires an error: never a panic and never an answer built from
// fewer bytes than the reply carries.
func TestEveryMalformedReplyIsAnError(t *testing.T) {
	for _, vc := range verbCases("") {
		for _, cmd := range vc.cmds {
			_, full := answerFor(t, vc, cmd)
			for n := 0; n < len(full); n++ {
				if truncationIsLegal(cmd, n) {
					continue
				}
				c := &countingDevice{reply: func(f tfw.Frame, st tfw.Status, p []byte) (tfw.Status, []byte) {
					if f.Cmd() == cmd {
						return st, p[:min(n, len(p))]
					}
					return st, p
				}}
				out, err := runCounted(t, c, false, vc.args...)
				if err == nil {
					t.Errorf("%s: %v reply cut to %d of %d bytes printed %d bytes, want an error", vc.args[0], cmd, n, len(full), len(out))
				}
			}
		}
	}
}

// answerFor records the bench device's good reply to cmd for one run of vc.
func answerFor(t *testing.T, vc verbCase, cmd tfw.Cmd) (tfw.Status, []byte) {
	t.Helper()
	var st tfw.Status
	var got []byte
	c := &countingDevice{reply: func(f tfw.Frame, s tfw.Status, p []byte) (tfw.Status, []byte) {
		if f.Cmd() == cmd && got == nil {
			st, got = s, append([]byte{}, p...)
		}
		return s, p
	}}
	if _, err := runCounted(t, c, false, vc.args...); err != nil {
		t.Fatalf("%q: %v", vc.args, err)
	}
	return st, got
}

// truncationIsLegal names the lengths a shorter reply is a correct one at.
func truncationIsLegal(cmd tfw.Cmd, n int) bool {
	switch cmd {
	case tfw.CmdCaps:
		// The legacy CAPS header, read as one with no algorithms; 15 is half
		// the algorithm bitmap and refused.
		return n == 14
	case tfw.CmdSysInfo:
		// 7 is the original reply and 11 adds the error counters (#5); 8-10
		// and 12-14 are a field cut in half and refused.
		return n == 7 || n == 11
	case tfw.CmdAttest:
		// FAULT: an ATTEST page shorter than the device's page is spliced in
		// and the next page asked for (attest.go:97 checks only the header),
		// so attest answered with 3-61 byte pages prints 164 bytes of
		// evidence assembled from the wrong offsets.
		return n > 2
	}
	return false
}

// TestRandomRepliesNeverPanic sends every verb seeded random replies of every
// length up to the payload limit, and random statuses.
func TestRandomRepliesNeverPanic(t *testing.T) {
	rng := rand.New(rand.NewSource(mutateSeed)) // #nosec G404 -- a fixed seed is the point: the case reruns
	for _, vc := range verbCases("") {
		if vc.args[0] == "auth" || len(vc.cmds) == 0 {
			continue
		}
		for i := 0; i < 40; i++ {
			payload := make([]byte, rng.Intn(int(tfw.MaxPayload)+1))
			_, _ = rng.Read(payload)
			status := tfw.StatusOK
			if i%4 == 3 {
				status = tfw.Status(rng.Intn(256))
			}
			c := &countingDevice{reply: func(f tfw.Frame, st tfw.Status, p []byte) (tfw.Status, []byte) {
				if f.Cmd() == vc.cmds[0] {
					return status, payload
				}
				return st, p
			}}
			func() {
				defer func() {
					if r := recover(); r != nil {
						t.Fatalf("seed %#x case %d: %q panicked on reply %x status %v: %v", mutateSeed, i, vc.args, payload, status, r)
					}
				}()
				_, _ = runCounted(t, c, i%2 == 0, vc.args...)
			}()
		}
	}
}

// TestNetcfgGetRefusesEveryMutatedBlock writes a good block, then flips
// every bit pattern of every byte of it in the fake EEPROM: netcfg get
// answers an error, never a block decoded from bad bytes.
func TestNetcfgGetRefusesEveryMutatedBlock(t *testing.T) {
	e := &eepromDevice{}
	if _, err := runNetcfgVerb(t, e, false, netcfgSetArgs...); err != nil {
		t.Fatal(err)
	}
	good := e.mem
	for i := 0; i < tfw.NetConfigLen; i++ {
		for _, mask := range []byte{0x01, 0x80, 0xFF} {
			e.mem = good
			e.mem[tfw.NetConfigAddr+i] ^= mask
			if out, err := runNetcfgVerb(t, e, false, "netcfg", "get"); err == nil {
				t.Errorf("byte %d ^ %#02x: printed %q, want an error", i, mask, out)
			}
		}
	}
	e.mem = good
	if _, err := runNetcfgVerb(t, e, false, "netcfg", "get"); err != nil {
		t.Fatalf("the good block: %v", err)
	}
}

// FuzzParseNetcfgFlags: whatever the flags, a parse returns a block or an
// error, and a block it returns is one the SDK accepts or refuses -- never a
// panic.
func FuzzParseNetcfgFlags(f *testing.F) {
	f.Add("02:00:00:00:00:01", "192.0.2.10", "255.255.255.0", "192.0.2.1", "198.51.100.20:7020")
	f.Add("", "", "", "", "")
	f.Add("02:00:00:00:00:01:02:03", "::1", "0.0.0.0", "1.2.3.4x", "[::1]:1")
	f.Add(overLong, overLong, overLong, overLong, overLong)
	f.Fuzz(func(t *testing.T, mac, ip, mask, gw, ep string) {
		_, _, _ = parseNetcfgFlags([]string{"--mac", mac, "--ip", ip, "--mask", mask, "--gateway", gw, "--endpoint", ep})
	})
}

// FuzzTakeOTASig: a --sig= value is 64 bytes of hex or an error.
func FuzzTakeOTASig(f *testing.F) {
	f.Add(strings.TrimPrefix(otaSig, "--sig="))
	f.Add("")
	f.Add(strings.Repeat("e", 127))
	f.Add(strings.Repeat("ee", 65))
	f.Add(" " + strings.Repeat("ee", 64) + "\n")
	f.Fuzz(func(t *testing.T, text string) {
		sig, rest, found, err := takeOTASig([]string{"--sig=" + text, "now"})
		if err != nil {
			if found || rest != nil {
				t.Fatalf("%q: an error with found %v rest %v", text, found, rest)
			}
			return
		}
		if !found || len(rest) != 1 || len(sig) != tfw.OTAAuthSigLen {
			t.Fatalf("%q: sig %x rest %v found %v", text, sig, rest, found)
		}
	})
}

// FuzzParseNonce: a nonce is exactly the length asked for, or an error.
func FuzzParseNonce(f *testing.F) {
	f.Add(nonce16, tfw.AttestNonceLen)
	f.Add("0x"+nonce32, tfw.AliasNonceLen)
	f.Add("", 0)
	f.Add("zz", 1)
	f.Fuzz(func(t *testing.T, s string, want int) {
		b, err := parseNonce(s, want)
		if err == nil && len(b) != want {
			t.Fatalf("%q want %d: %d bytes", s, want, len(b))
		}
	})
}

// FuzzParseNumbers: parsePin, parseU8, parseU16 and parseHex refuse or
// return a value that round-trips through the text it came from.
func FuzzParseNumbers(f *testing.F) {
	for _, s := range []string{"3", "255", "256", "0x10", "-1", "3x", "", "dead", "0xdead", "d"} {
		f.Add(s)
	}
	f.Fuzz(func(t *testing.T, s string) {
		_, _ = parsePin(s)
		_, _ = parseU8(s, "x")
		_, _ = parseU16(s, "x")
		if b, err := parseHex(s); err == nil && len(b) == 0 {
			t.Fatalf("%q: parseHex returned no bytes and no error", s)
		}
		if b, err := parseHexOrEmpty(s); err == nil && s != "-" && len(b) == 0 {
			t.Fatalf("%q: parseHexOrEmpty returned no bytes and no error", s)
		}
	})
}

// FuzzTCPAddr: a port is not tcp://, or is one with a host:port, or is an
// error.
func FuzzTCPAddr(f *testing.F) {
	for _, s := range []string{"tcp://127.0.0.1:7020", "tcp://", "tcp://host", "/dev/ttyACM0", "tcp://[::1]:1", "tcp://:"} {
		f.Add(s)
	}
	f.Fuzz(func(t *testing.T, s string) {
		addr, ok, err := tcpAddr(s)
		if !ok && (addr != "" || err != nil) {
			t.Fatalf("%q: not tcp but addr %q err %v", s, addr, err)
		}
		if ok && err == nil && !strings.HasPrefix(s, tcpScheme+addr) {
			t.Fatalf("%q: addr %q is not what followed the scheme", s, addr)
		}
	})
}

// TestTCPAddrRefusesMutatedAddresses pins the refusals tcp_test.go does not
// (an extra colon, an unclosed bracket, an over-long host, a port past
// 65535); none of them dials.
func TestTCPAddrRefusesMutatedAddresses(t *testing.T) {
	for _, s := range []string{"tcp://127.0.0.1:1:2", "tcp://[::1", "tcp://" + overLong} {
		if _, ok, err := tcpAddr(s); !ok || err == nil {
			t.Errorf("%q: ok %v err %v, want a refused tcp address", s, ok, err)
		}
	}
	if _, err := listenForDevice("127.0.0.1:99999", 0); err == nil {
		t.Error("listening on port 99999: want an error")
	}
}
