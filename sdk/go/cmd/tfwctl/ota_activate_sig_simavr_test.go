//go:build simavr

// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"crypto/sha256"
	"encoding/binary"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// simavrElementKey is EL_SIGNING_KEY in test/integration/simavr/boot_harness.c,
// the modelled element's key that signs every image of the simavr suite.
var simavrElementKey = [32]byte{
	0x9E, 0x1C, 0x4B, 0x22, 0x70, 0xA5, 0x33, 0xF8, 0x0D, 0x61, 0xB4, 0x17, 0xCA, 0x2E, 0x88, 0x5D,
	0x30, 0x79, 0xE6, 0x44, 0x12, 0xBB, 0x57, 0x9A, 0xC3, 0x08, 0x6F, 0xD1, 0x25, 0xAE, 0x73, 0x40,
}

// simavrActivateSig is the harness's sign_activate for an image as the stager
// holds it, its 128-byte trailer last: the version (offset 8, little-endian)
// and measurement (offset 16) from that trailer, mode 0, and element_sign's
// construction over the structure's SHA-256 -- SHA-256(key||digest) then
// SHA-256(digest||key), the model the harness's element verifies.
func simavrActivateSig(img []byte) [tfw.OTAAuthSigLen]byte {
	tr := img[len(img)-128:]
	var m [32]byte
	copy(m[:], tr[16:48])
	d := sha256.Sum256(activateSigStructure(m, binary.LittleEndian.Uint32(tr[8:12]), 0))
	var sig [tfw.OTAAuthSigLen]byte
	a := sha256.Sum256(append(append([]byte{}, simavrElementKey[:]...), d[:]...))
	b := sha256.Sum256(append(append([]byte{}, d[:]...), simavrElementKey[:]...))
	copy(sig[:32], a[:])
	copy(sig[32:], b[:])
	return sig
}
