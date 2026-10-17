// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"encoding/hex"
	"testing"
)

// activateSigStructure is the COSE Sig_structure (RFC 9052 §4.4) of the
// OTA_ACTIVATE authorization, built as cose_activate (boot/boot_stage.c) builds
// it, ES256 in the protected header.
func activateSigStructure(measurement [32]byte, version uint32, mode byte) []byte {
	p := append([]byte{0x84, 0x70}, "tfw-ota-activate"...)
	p = append(p, 0x58, 0x20)
	p = append(p, measurement[:]...)
	switch {
	case version < 24:
		p = append(p, byte(version))
	case version <= 0xFF:
		p = append(p, 0x18, byte(version))
	case version <= 0xFFFF:
		p = append(p, 0x19, byte(version>>8), byte(version))
	default:
		p = append(p, 0x1A, byte(version>>24), byte(version>>16), byte(version>>8), byte(version))
	}
	p = append(p, mode)
	b := append([]byte{0x84, 0x6A}, "Signature1"...)
	b = append(b, 0x43, 0xA1, 0x01, 0x26, 0x40, 0x58, byte(len(p)))
	return append(b, p...)
}

// The builder against TFWV_OTA_ACTIVATE_SIG_STRUCTURE (testdata/vectors.h), as
// its Go mirror in sdk/go/ota_auth_vectors_test.go holds it.
func TestActivateSigStructureMatchesTheVector(t *testing.T) {
	const vector = "846a5369676e61747572653143a1012640583684707466772d6f74612d61637469766174655820000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f0700"
	var m [32]byte
	for i := range m {
		m[i] = byte(i)
	}
	want, _ := hex.DecodeString(vector)
	if got := activateSigStructure(m, 7, 0); !bytes.Equal(got, want) {
		t.Fatalf("structure %x, want %x", got, want)
	}
}
