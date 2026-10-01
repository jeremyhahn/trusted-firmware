// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRunBakeEEPROMRejectsAnAllZeroSerial(t *testing.T) {
	out := filepath.Join(t.TempDir(), "platform.eep")
	err := runBakeEEPROM(out, "00000000000000000000000000000000", "")
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
	if err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", ""); err != nil {
		t.Fatalf("runBakeEEPROM: %v", err)
	}
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatalf("reading %s: %v", out, err)
	}
	if len(data) == 0 {
		t.Fatal("wrote an empty file")
	}
	if !strings.HasPrefix(string(data), ":") {
		t.Fatalf("first byte is not an Intel HEX record start: %q", data[:1])
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
	err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", strings.Repeat("00", 32))
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
	if err := runBakeEEPROM(out, "000102030405060708090a0b0c0d0e0f", strings.Repeat("ab", 32)); err != nil {
		t.Fatalf("runBakeEEPROM with a real hash: %v", err)
	}
}
