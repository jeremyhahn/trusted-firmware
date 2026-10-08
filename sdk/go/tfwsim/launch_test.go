// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The identity options: what each source of a device's identity contributes,
// and that a bad one stops the simulator rather than being skipped.

func attestFrame(t *testing.T, d *tfw.Device) []byte {
	t.Helper()
	ev, err := d.Attest(context.Background(), attestNonce(0x21))
	if err != nil {
		t.Fatal(err)
	}
	return ev
}

func writeFile(t *testing.T, name string, data []byte) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(p, data, 0o600); err != nil {
		t.Fatal(err)
	}
	return p
}

func hashArg() []string {
	return []string{"--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}
}

// The real provisioning artefact: `tfwprov --bake-eeprom` writes Intel HEX, and
// the simulator has to read exactly that rather than a format of its own.
func TestElementBootsFromATfwprovBakedEEPROM(t *testing.T) {
	serial := "00112233445566778899aabbccddeeff"
	eep := filepath.Join(t.TempDir(), "device.eep")

	cmd := exec.Command("go", "run", "../cmd/tfwprov", "--bake-eeprom", "--serial", serial, "--out", eep)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("tfwprov --bake-eeprom: %v\n%s", err, out)
	}

	d := tfwsim.StartElementWith(t, append([]string{"--eeprom", eep}, hashArg()...))
	ev := attestFrame(t, d)

	if got := hex.EncodeToString(ev[offDevice : offDevice+tfw.SerialLen]); got != serial {
		t.Errorf("DeviceID = %s, want the baked %s", got, serial)
	}
	if !bytes.Equal(ev[offFWHash:offFWHash+32], tfwsim.TestFWHash[:]) {
		t.Errorf("FWHash = %x, want %x", ev[offFWHash:offFWHash+32], tfwsim.TestFWHash)
	}
}

// Zero rather than fabricated, wherever a source is absent: a verifier must be
// able to see an unprovisioned platform for what it is.
func TestElementWithoutAPlatformBlockReportsAZeroDeviceID(t *testing.T) {
	ev := attestFrame(t, tfwsim.StartElementWith(t, hashArg()))
	if !bytes.Equal(ev[offDevice:offDevice+tfw.SerialLen], make([]byte, tfw.SerialLen)) {
		t.Errorf("DeviceID = %x, want zero from an erased EEPROM", ev[offDevice:offDevice+tfw.SerialLen])
	}
}

func TestElementWithACorruptPlatformBlockReportsAZeroDeviceID(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	block[60] ^= 0xFF // inside the serial, behind the CRC
	eep := writeFile(t, "bad.bin", block[:])

	ev := attestFrame(t, tfwsim.StartElementWith(t, append([]string{"--eeprom-bin", eep}, hashArg()...)))
	if !bytes.Equal(ev[offDevice:offDevice+tfw.SerialLen], make([]byte, tfw.SerialLen)) {
		t.Errorf("DeviceID = %x, want zero for a block that fails its CRC", ev[offDevice:offDevice+tfw.SerialLen])
	}
}

func TestElementWithoutAHandoffReportsAZeroFWHash(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := writeFile(t, "platform.bin", block[:])

	ev := attestFrame(t, tfwsim.StartElementWith(t, []string{"--eeprom-bin", eep, "--provision-element"}))
	if !bytes.Equal(ev[offFWHash:offFWHash+32], make([]byte, 32)) {
		t.Errorf("FWHash = %x, want zero with no tfw-boot handoff", ev[offFWHash:offFWHash+32])
	}
}

func TestElementWithADifferentImageReportsADifferentFWHash(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := writeFile(t, "platform.bin", block[:])
	other := strings.Repeat("ab", 32)

	ev := attestFrame(t, tfwsim.StartElementWith(t,
		[]string{"--eeprom-bin", eep, "--provision-element", "--image-hash", other}))
	if got := hex.EncodeToString(ev[offFWHash : offFWHash+32]); got != other {
		t.Errorf("FWHash = %s, want %s", got, other)
	}
}

// ihex builds one Intel HEX data record plus the EOF record, with its checksum
// either right or deliberately wrong.
func ihex(addr uint16, data []byte, goodSum bool) string {
	rec := append([]byte{byte(len(data)), byte(addr >> 8), byte(addr), 0x00}, data...)
	var sum byte
	for _, b := range rec {
		sum += b
	}
	sum = -sum
	if !goodSum {
		sum++
	}
	return ":" + strings.ToUpper(hex.EncodeToString(append(rec, sum))) + "\n:00000001FF\n"
}

func runSim(t *testing.T, bin simproc.Binary, args ...string) (int, string) {
	t.Helper()
	path, err := simproc.Locate(bin)
	if err != nil {
		t.Fatal(err)
	}
	var stderr bytes.Buffer
	cmd := exec.Command(path, args...)
	cmd.Stdin = strings.NewReader("") // EOF: a simulator that starts cleanly exits 0 at once
	cmd.Stderr = &stderr
	err = cmd.Run()
	var exit *exec.ExitError
	switch {
	case err == nil:
		return 0, stderr.String()
	case errors.As(err, &exit):
		return exit.ExitCode(), stderr.String()
	}
	t.Fatal(err)
	return -1, ""
}

// A simulator that quietly ignored a bad identity would start unprovisioned and
// let a test pass or fail for a reason nobody printed, so every bad option is a
// refusal to start that names the option.
func TestElementRefusesToStartOnABadOption(t *testing.T) {
	good := ihex(0, []byte{1, 2, 3, 4}, true)
	cases := []struct {
		name string
		args []string
		want string
	}{
		{"unknown option", []string{"--bogus"}, "unknown option --bogus"},
		{"image hash not hex", []string{"--image-hash", strings.Repeat("zz", 32)}, "--image-hash"},
		{"image hash too short", []string{"--image-hash", "abcd"}, "--image-hash"},
		{"image hash missing value", []string{"--image-hash"}, "needs a value"},
		{"eeprom missing value", []string{"--eeprom"}, "needs a value"},
		{"eeprom file absent", []string{"--eeprom", filepath.Join(t.TempDir(), "none.eep")}, "cannot open"},
		{"eeprom not hex", []string{"--eeprom", writeFile(t, "a.eep", []byte("not intel hex"))}, "Intel HEX"},
		{"eeprom bad checksum", []string{"--eeprom", writeFile(t, "b.eep", []byte(ihex(0, []byte{1, 2, 3, 4}, false)))}, "Intel HEX"},
		{"eeprom no EOF record", []string{"--eeprom", writeFile(t, "c.eep", []byte(strings.SplitN(good, "\n", 2)[0]+"\n"))}, "Intel HEX"},
		{"eeprom past the end of the part", []string{"--eeprom", writeFile(t, "d.eep", []byte(ihex(0x03FE, []byte{1, 2, 3, 4}, true)))}, "Intel HEX"},
		{"raw eeprom larger than the part", []string{"--eeprom-bin", writeFile(t, "e.bin", make([]byte, 2048))}, "larger than the EEPROM"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			code, stderr := runSim(t, simproc.Element, tc.args...)
			if code != 2 {
				t.Errorf("exit code = %d, want 2", code)
			}
			if !strings.Contains(stderr, tc.want) {
				t.Errorf("stderr %q does not mention %q", stderr, tc.want)
			}
		})
	}
}

func TestElementAcceptsAWellFormedHexFile(t *testing.T) {
	eep := writeFile(t, "ok.eep", []byte(ihex(0, []byte{1, 2, 3, 4}, true)))
	if code, stderr := runSim(t, simproc.Element, "--eeprom", eep); code != 0 {
		t.Errorf("exit code = %d, want 0: %s", code, stderr)
	}
}

// The software tier has no identity options, and must say so rather than
// starting a device that never saw the file.
func TestSoftwareTierRefusesTheIdentityOptions(t *testing.T) {
	code, stderr := runSim(t, simproc.Software, "--eeprom", "x.eep")
	if code != 2 || !strings.Contains(stderr, "unknown option --eeprom") {
		t.Errorf("exit %d, stderr %q; want exit 2 naming the option", code, stderr)
	}
}
