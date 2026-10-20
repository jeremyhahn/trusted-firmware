// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// The simulator's identity options, read back through `tfwctl attest`: what
// each source of a device's identity contributes, and that a bad one stops the
// simulator rather than being skipped. Simulator-only, so `make hw-test` does
// not run these: the options are the simulator's launch flags, and a board
// has none.

func writeFile(t *testing.T, name string, data []byte) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	require.NoError(t, os.WriteFile(p, data, 0o600))
	return p
}

func hashArg() []string {
	return []string{"--provision-element", "--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}
}

func launchFrame(t *testing.T, args ...string) []byte {
	t.Helper()
	return cliAttest(t, tfwsim.StartElementWith(t, args), attestNonce(0x21))
}

// The real provisioning artefact: `tfwprov --bake-eeprom` writes Intel HEX, and
// the simulator has to read exactly that rather than a format of its own.
func TestE2EElementBootsFromATfwprovBakedEEPROM(t *testing.T) {
	serial := "00112233445566778899aabbccddeeff"
	eep := filepath.Join(t.TempDir(), "device.eep")

	cmd := exec.Command("go", "run", "../tfwprov", "--bake-eeprom", "--serial", serial, "--out", eep)
	out, err := cmd.CombinedOutput()
	require.NoError(t, err, "tfwprov --bake-eeprom: %s", out)

	ev := launchFrame(t, append([]string{"--eeprom", eep}, hashArg()...)...)
	assert.Equal(t, serial, hex.EncodeToString(ev[offDevice:offDevice+tfw.SerialLen]), "DeviceID, want the baked serial")
	assert.Equal(t, tfwsim.TestFWHash[:], ev[offFWHash:offFWHash+32], "FWHash")
}

// Zero rather than fabricated, wherever a source is absent: a verifier must be
// able to see an unprovisioned platform for what it is.
func TestE2EElementWithoutAPlatformBlockReportsAZeroDeviceID(t *testing.T) {
	ev := launchFrame(t, hashArg()...)
	assert.Equal(t, make([]byte, tfw.SerialLen), ev[offDevice:offDevice+tfw.SerialLen], "DeviceID from an erased EEPROM")
}

func TestE2EElementWithACorruptPlatformBlockReportsAZeroDeviceID(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	block[60] ^= 0xFF // inside the serial, behind the CRC
	eep := writeFile(t, "bad.bin", block[:])

	ev := launchFrame(t, append([]string{"--eeprom-bin", eep}, hashArg()...)...)
	assert.Equal(t, make([]byte, tfw.SerialLen), ev[offDevice:offDevice+tfw.SerialLen], "DeviceID for a block that fails its CRC")
}

func TestE2EElementWithoutAHandoffReportsAZeroFWHash(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := writeFile(t, "platform.bin", block[:])

	ev := launchFrame(t, "--eeprom-bin", eep, "--provision-element")
	assert.Equal(t, make([]byte, 32), ev[offFWHash:offFWHash+32], "FWHash with no tfw-boot handoff")
}

func TestE2EElementWithADifferentImageReportsADifferentFWHash(t *testing.T) {
	block := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	eep := writeFile(t, "platform.bin", block[:])
	other := strings.Repeat("ab", 32)

	ev := launchFrame(t, "--eeprom-bin", eep, "--provision-element", "--image-hash", other)
	assert.Equal(t, other, hex.EncodeToString(ev[offFWHash:offFWHash+32]), "FWHash")
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

// runSim runs a simulator binary on its own command line and returns its exit
// code and stderr.
func runSim(t *testing.T, bin simproc.Binary, args ...string) (int, string) {
	t.Helper()
	path, err := simproc.Locate(bin)
	require.NoError(t, err)
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
func TestE2EElementRefusesToStartOnABadOption(t *testing.T) {
	good := ihex(0, []byte{1, 2, 3, 4}, true)
	for _, tc := range []struct {
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
	} {
		t.Run(tc.name, func(t *testing.T) {
			code, stderr := runSim(t, simproc.Element, tc.args...)
			assert.Equal(t, 2, code, "exit code")
			assert.Contains(t, stderr, tc.want)
		})
	}
}

func TestE2EElementAcceptsAWellFormedHexFile(t *testing.T) {
	eep := writeFile(t, "ok.eep", []byte(ihex(0, []byte{1, 2, 3, 4}, true)))
	code, stderr := runSim(t, simproc.Element, "--eeprom", eep)
	assert.Equal(t, 0, code, "exit code: %s", stderr)
}

// The software tier has no identity options, and must say so rather than
// starting a device that never saw the file.
func TestE2ESoftwareTierRefusesTheIdentityOptions(t *testing.T) {
	code, stderr := runSim(t, simproc.Software, "--eeprom", "x.eep")
	assert.Equal(t, 2, code, "exit code")
	assert.Contains(t, stderr, "unknown option --eeprom")
}
