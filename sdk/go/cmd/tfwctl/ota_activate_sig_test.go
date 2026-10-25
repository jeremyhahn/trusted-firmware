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
	return sigStructure(append(otaAuthBody(p, measurement, version), mode))
}

// confirmSigStructure is the OTA_CONFIRM authorization's Sig_structure, built
// as tfw_boot_cose_confirm (boot/boot_cose.c) builds it: the running image's
// measurement and version, and no mode.
func confirmSigStructure(measurement [32]byte, version uint32) []byte {
	p := append([]byte{0x83, 0x6F}, "tfw-ota-confirm"...)
	return sigStructure(otaAuthBody(p, measurement, version))
}

// otaAuthBody appends the measurement and the version, as CBOR, to a payload
// that opens with its array header and domain tag.
func otaAuthBody(p []byte, measurement [32]byte, version uint32) []byte {
	p = append(p, 0x58, 0x20)
	p = append(p, measurement[:]...)
	switch {
	case version < 24:
		return append(p, byte(version))
	case version <= 0xFF:
		return append(p, 0x18, byte(version))
	case version <= 0xFFFF:
		return append(p, 0x19, byte(version>>8), byte(version))
	default:
		return append(p, 0x1A, byte(version>>24), byte(version>>16), byte(version>>8), byte(version))
	}
}

// sigStructure wraps a payload in the Sig_structure: "Signature1", the
// protected header h'A10126', no external AAD, and the payload as a bstr.
func sigStructure(p []byte) []byte {
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

// The confirm builder against the bytes tfw_boot_cose_confirm (boot/boot_cose.c)
// builds and test/unit/boot/test_boot_cose_mutate.c pins, for measurement
// 00..1f and version 7: ["tfw-ota-confirm", measurement, version], three
// elements and no mode (docs/ota/api.md). An activate signature never
// confirms (#231), so the two structures never sign the same bytes.
func TestConfirmSigStructureIsTfwBoots(t *testing.T) {
	const vector = "846a5369676e61747572653143a10126405834836f7466772d6f74612d636f6e6669726d5820000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f07"
	var m [32]byte
	for i := range m {
		m[i] = byte(i)
	}
	want, _ := hex.DecodeString(vector)
	got := confirmSigStructure(m, 7)
	if !bytes.Equal(got, want) {
		t.Fatalf("structure %x, want %x", got, want)
	}
	if bytes.Equal(got, activateSigStructure(m, 7, 0)) {
		t.Fatal("the confirm structure is the activate structure")
	}
}
