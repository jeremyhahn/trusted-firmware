package main

import (
	"bytes"
	"errors"
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// mainEnv set to 1 makes the test binary run main() with the arguments after
// "--" instead of the tests: the only way to observe main's exit codes, which
// it reaches through os.Exit (main.go:152 usage, main.go:250 fail).
const mainEnv = "TFWPROV_TEST_RUN_MAIN"

func TestMain(m *testing.M) {
	if os.Getenv(mainEnv) == "1" {
		for i, a := range os.Args {
			if a == "--" {
				os.Args = append([]string{"tfwprov"}, os.Args[i+1:]...)
				break
			}
		}
		flag.CommandLine = flag.NewFlagSet("tfwprov", flag.ExitOnError)
		main()
		os.Exit(0)
	}
	os.Exit(m.Run())
}

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
	dir := t.TempDir()
	out := filepath.Join(dir, "out.eep")
	port := filepath.Join(dir, "no-such-port")
	tmpl := writeTemplate(t, goodTemplate+block32+"\n")
	serial := strings.Repeat("00", 16)
	const usageLine = "tfwprov -- provision an trusted-firmware device's secure element"
	for _, tc := range []struct {
		name  string
		args  []string
		code  int
		first string
	}{
		{"no arguments", nil, 2, usageLine},
		{"an unknown flag", []string{"--bogus"}, 2, "flag provided but not defined: -bogus"},
		{"two modes", []string{"--dry-run", "--verify", port}, 2, usageLine},
		{"a mode with no port", []string{"--dry-run"}, 2, usageLine},
		{"--force", []string{"--force", "--commit", port}, 1, "tfwprov: --force is not accepted; --commit requires --yes-i-mean-it"},
		{"bake with a wire mode", []string{"--bake-eeprom", "--dry-run"}, 1, "tfwprov: --bake-eeprom is its own mode; it does not combine with --dry-run/--verify/--commit"},
		{"bake with a port", []string{"--bake-netblock", port}, 1, "tfwprov: --bake-netblock takes no positional argument"},
		{"bake with no --out", []string{"--bake-eeprom"}, 1, "tfwprov: --bake-eeprom requires --out"},
		{"bake-eeprom with no --serial", []string{"--bake-eeprom", "--out", out}, 1, "tfwprov: --bake-eeprom requires --serial (16 bytes, hex)"},
		{"bake-netblock with --serial", []string{"--bake-netblock", "--out", out, "--serial", serial}, 1, "tfwprov: --serial/--pubkey-hash/--operator-pubkey belong to --bake-eeprom"},
		{"bake-eeprom with --mac", []string{"--bake-eeprom", "--out", out, "--serial", serial, "--mac", "02:00:00:00:00:01"}, 1, "tfwprov: --mac/--ip/--mask/--gateway/--endpoint/--dhcp belong to --bake-netblock"},
		{"bake-eeprom with a bad serial", []string{"--bake-eeprom", "--out", out, "--serial", "zz"}, 1, "tfwprov: tfwprov: --serial: encoding/hex: invalid byte"},
		{"a wire mode with --out", []string{"--dry-run", "--out", out, port}, 1, "tfwprov: --out belongs to --bake-eeprom"},
		{"a wire mode with --mac", []string{"--dry-run", "--mac", "02:00:00:00:00:01", port}, 1, "tfwprov: --mac/--ip/--mask/--gateway/--endpoint/--dhcp belong to --bake-netblock; --dry-run/--verify/--commit do not use them"},
		{"bake-netblock with --record", []string{"--bake-netblock", "--out", out, "--record", filepath.Join(dir, "r.json")}, 1, "tfwprov: --record needs the serial: pass --bake-eeprom --serial"},
		{"dry-run with --record", []string{"--dry-run", "--record", filepath.Join(dir, "r.json"), port}, 1, "tfwprov: --record is written by --bake-eeprom, --commit and --verify"},
		{"commit without confirmation", []string{"--commit", port}, 1, "tfwprov: --commit is irreversible on this part: pass --yes-i-mean-it"},
		{"no --config", []string{"--dry-run", port}, 1, "tfwprov: --config is required"},
		{"a missing template", []string{"--dry-run", "--config", filepath.Join(dir, "absent.tmpl"), port}, 1, "tfwprov: open "},
		{"a port that cannot be opened", []string{"--verify", "--config", tmpl, port}, 1, "tfwprov: serialport: opening "},
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

// --bake-netblock --dhcp leases the address, mask and gateway, so the binary
// refuses each of them beside it, exits 1 and writes no image.
func TestMainBakeNetblockDHCPRefusesAStaticAddress(t *testing.T) {
	const refusal = "--dhcp leases the address, mask and gateway; --ip, --mask and --gateway go without it"
	for _, extra := range [][]string{
		{"--ip", "198.51.100.22/24"},
		{"--mask", "255.255.255.0"},
		{"--gateway", "198.51.100.1"},
	} {
		t.Run(extra[0], func(t *testing.T) {
			out := filepath.Join(t.TempDir(), "net.eep")
			args := append([]string{"--bake-netblock", "--dhcp", "--mac", "02:00:00:00:00:16",
				"--endpoint", "198.51.100.1:7020", "--out", out}, extra...)
			code, first := runMain(t, args...)
			if code != 1 {
				t.Errorf("exit code = %d, want 1", code)
			}
			if !strings.HasPrefix(first, "tfwprov: ") || !strings.Contains(first, refusal) {
				t.Errorf("stderr first line = %q, want the refusal %q", first, refusal)
			}
			if _, err := os.Stat(out); !errors.Is(err, os.ErrNotExist) {
				t.Errorf("%s: stat err %v, want no image written", out, err)
			}
		})
	}
	out := filepath.Join(t.TempDir(), "net.eep")
	if code, first := runMain(t, "--bake-netblock", "--dhcp", "--mac", "02:00:00:00:00:16",
		"--endpoint", "198.51.100.1:7020", "--out", out); code != 0 {
		t.Fatalf("--bake-netblock --dhcp alone: exit %d, stderr %q", code, first)
	}
	if _, err := os.Stat(out); err != nil {
		t.Fatalf("--bake-netblock --dhcp alone wrote no image: %v", err)
	}
}
