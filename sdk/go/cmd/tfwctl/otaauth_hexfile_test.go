// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// TestTakeOTASigReadsAHexFileAsHexAtAnyLength: a file of 64 hex digits is half
// the authorization in hex, not 64 raw bytes, and is refused before anything is
// sent; a raw 64-byte file that is not all hex digits still loads as raw.
func TestTakeOTASigReadsAHexFileAsHexAtAnyLength(t *testing.T) {
	dir := t.TempDir()
	halfPath := filepath.Join(dir, "half.hex")
	if err := os.WriteFile(halfPath, []byte(strings.Repeat("ab", 32)), 0o600); err != nil {
		t.Fatal(err)
	}
	_, rest, found, err := takeOTASig([]string{"--sig-file=" + halfPath})
	if err == nil || !strings.Contains(err.Error(), "the authorization is 64 bytes") || found || rest != nil {
		t.Fatalf("64 hex digits: rest %v, found %v, err %v, want refused as 32 bytes", rest, found, err)
	}

	raw := make([]byte, tfw.OTAAuthSigLen)
	for i := range raw {
		raw[i] = byte(i)
	}
	rawPath := filepath.Join(dir, "sig.bin")
	if err := os.WriteFile(rawPath, raw, 0o600); err != nil {
		t.Fatal(err)
	}
	sig, _, found, err := takeOTASig([]string{"--sig-file=" + rawPath})
	if err != nil || !found || !bytes.Equal(sig[:], raw) {
		t.Fatalf("raw 0x00..0x3F: sig %x, found %v, err %v", sig, found, err)
	}
}
