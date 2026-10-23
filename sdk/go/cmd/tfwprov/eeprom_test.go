// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"crypto/ecdh"
	"crypto/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
)

func TestRunBakeEEPROMRejectsAnAllZeroSerial(t *testing.T) {
	out := filepath.Join(t.TempDir(), "platform.eep")
	err := runBakeEEPROM(out, "00000000000000000000000000000000", "", "")
	if err == nil {
		t.Fatal("expected an error for an all-zero serial, got none")
	}
	if !strings.Contains(err.Error(), "all zero") {
		t.Fatalf("error = %q, want it to name the all-zero refusal", err)
	}
	// And nothing was written -- the refusal happens before any file touch.
	if _, statErr := os.Stat(out); !os.IsNotExist(statErr) {
		t.Fatalf("expected no file at %s, stat returned %v", out, statErr)
	}
}

func TestRunBakeEEPROMWritesARealSerial(t *testing.T) {
	out := filepath.Join(t.TempDir(), "platform.eep")
	if err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", "", ""); err != nil {
		t.Fatalf("runBakeEEPROM: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatalf("reading %s: %v", out, err)
	}
	image := readIHEX(t, string(data))
	want := tfw.BuildPlatformBlock([tfw.SerialLen]byte{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		[tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if !bytes.Equal(image, want[:]) {
		t.Fatalf("image = % x\nwant    % x", image, want)
	}
}

func TestAllZero(t *testing.T) {
	if !allZero(make([]byte, 16)) {
		t.Fatal("16 zero bytes should be all-zero")
	}
	nonZero := make([]byte, 16)
	nonZero[15] = 0x01
	if allZero(nonZero) {
		t.Fatal("a buffer with one non-zero byte should not be all-zero")
	}
}

// An explicit all-zero hash would bake the same unbootable block the omitted
// flag warns about, without the warning. It is refused, and nothing is written.
func TestRunBakeEEPROMRejectsAnExplicitAllZeroPubkeyHash(t *testing.T) {
	out := filepath.Join(t.TempDir(), "platform.eep")
	err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", strings.Repeat("00", 32), "")
	if err == nil {
		t.Fatal("expected an error for an explicit all-zero --pubkey-hash, got none")
	}
	if !strings.Contains(err.Error(), "pubkey-hash is all zero") {
		t.Fatalf("error = %q, want it to name the all-zero pubkey-hash refusal", err)
	}
	if _, statErr := os.Stat(out); !os.IsNotExist(statErr) {
		t.Fatalf("expected no file at %s, stat returned %v", out, statErr)
	}
}

func TestRunBakeEEPROMAcceptsARealPubkeyHash(t *testing.T) {
	out := filepath.Join(t.TempDir(), "platform.eep")
	if err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", strings.Repeat("ab", 32), ""); err != nil {
		t.Fatalf("runBakeEEPROM with a real hash: %v", err)
	}
}

func testP256Point(t *testing.T) []byte {
	t.Helper()
	key, err := ecdh.P256().GenerateKey(rand.Reader)
	if err != nil {
		t.Fatalf("generating a P-256 key: %v", err)
	}
	sec1 := key.PublicKey().Bytes() // 0x04 || X || Y, 65 bytes
	return sec1[1:]                 // X || Y, 64 bytes -- the wire's own format
}

func TestRequireOnCurveP256AcceptsARealPoint(t *testing.T) {
	if err := requireOnCurveP256(testP256Point(t)); err != nil {
		t.Fatalf("a real P-256 point was refused: %v", err)
	}
}

// The failure this exists to catch: 64 bytes that decode from hex cleanly
// (parseFixedHex has already accepted them by the time this runs) but are
// not coordinates of any point on the curve -- a single flipped hex digit
// from a real one is exactly this shape.
func TestRequireOnCurveP256RefusesGarbage(t *testing.T) {
	garbage := make([]byte, 64)
	for i := range garbage {
		garbage[i] = byte(i + 1)
	}
	if err := requireOnCurveP256(garbage); err == nil {
		t.Fatal("64 bytes with no relation to the curve were accepted")
	}
}

// A one-bit change to a real point must also be refused -- the case that
// looks the most like an operator's honest typo.
func TestRequireOnCurveP256RefusesATamperedRealPoint(t *testing.T) {
	pt := testP256Point(t)
	pt[0] ^= 0x01
	if err := requireOnCurveP256(pt); err == nil {
		t.Fatal("a tampered point was accepted")
	}
}

func TestRequireOnCurveP256RefusesTheWrongLength(t *testing.T) {
	if err := requireOnCurveP256(make([]byte, 63)); err == nil {
		t.Fatal("63 bytes was accepted")
	}
	if err := requireOnCurveP256(make([]byte, 65)); err == nil {
		t.Fatal("65 bytes was accepted")
	}
}
