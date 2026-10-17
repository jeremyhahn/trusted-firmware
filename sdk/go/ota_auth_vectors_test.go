// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"encoding/hex"
	"testing"
)

// otaActivateSigStructure mirrors TFWV_OTA_ACTIVATE_SIG_STRUCTURE in
// testdata/vectors.h: the COSE Sig_structure (RFC 9052 §4.4) of the
// OTA_ACTIVATE authorization tfw-boot verifies (#137), for ES256, measurement
// 00..1f, version 7, mode 0. Hand-written from the RFC; a literal on purpose.
const otaActivateSigStructure = "846a5369676e61747572653143a1012640583684707466772d6f74612d61637469766174655820000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f0700"

func TestOTAActivateSigStructureVector(t *testing.T) {
	b, err := hex.DecodeString(otaActivateSigStructure)
	if err != nil {
		t.Fatal(err)
	}
	if hex.EncodeToString(b) != otaActivateSigStructure || len(b) != 73 {
		t.Fatalf("round trip or length (%d) broken", len(b))
	}
	// The parts, each from its RFC rule: array(4), tstr "Signature1",
	// bstr h'A10126' (alg ES256, -7), empty external_aad, then the payload
	// bstr of 54 bytes: array(4), tstr(16), bstr(32), uint 7, uint 0.
	want := append([]byte{0x84, 0x6a}, "Signature1"...)
	want = append(want, 0x43, 0xa1, 0x01, 0x26, 0x40, 0x58, 0x36, 0x84, 0x70)
	want = append(want, "tfw-ota-activate"...)
	want = append(want, 0x58, 0x20)
	for i := 0; i < 32; i++ {
		want = append(want, byte(i))
	}
	want = append(want, 0x07, 0x00)
	if !bytes.Equal(b, want) {
		t.Fatalf("vector %x, want %x", b, want)
	}
}
