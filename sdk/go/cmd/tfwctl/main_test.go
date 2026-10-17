package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
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
		{"timer without a duration", "timer", []string{"13", "1"}, "usage: timer <pin> <0|1> <duration, e.g. 5s>"},
		{"timer with a bad pin", "timer", []string{"x", "1", "5s"}, `pin "x": `},
		{"timer with a bad duration", "timer", []string{"13", "1", "soon"}, `duration "soon": `},
		{"timer-cancel without a pin", "timer-cancel", nil, "usage: timer-cancel <pin> [apply]"},
		{"timer-cancel with a bad pin", "timer-cancel", []string{"x"}, `pin "x": `},
		{"timer-query without a pin", "timer-query", nil, "usage: timer-query <pin>"},
		{"timer-query with a bad pin", "timer-query", []string{"x"}, `pin "x": `},
		{"ota-stage without an image", "ota-stage", nil, "usage: ota-stage <image>"},
		{"ota-stage with two images", "ota-stage", []string{"a", "b"}, "usage: ota-stage <image>"},
		{"ota-stage with a missing image", "ota-stage", []string{missing}, "usage: ota-stage <image>: open "},
		{"ota-status with an argument", "ota-status", []string{"now"}, "usage: ota-status"},
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
