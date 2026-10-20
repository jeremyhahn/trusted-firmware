package main

import (
	"bytes"
	"context"
	"errors"
	"flag"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
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
