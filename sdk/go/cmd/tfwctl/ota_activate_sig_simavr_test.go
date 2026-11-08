//go:build simavr

// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"crypto/sha256"
	"encoding/binary"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// simavrElementKey is el_key() in test/integration/simavr/boot_harness.c, the
// modelled element's secret that signs every image of the simavr suite: the
// SHA-256 of the harness's EL_SIGNING_LABEL, made at test time on both sides.
var simavrElementKey = sha256.Sum256([]byte("boot_harness element signing secret"))

// simavrActivateSig is the harness's sign_activate for an image as the stager
// holds it, its 128-byte trailer last: the version (offset 8, little-endian)
// and measurement (offset 16) from that trailer, mode 0, and element_sign's
// construction over the structure's SHA-256 -- SHA-256(key||digest) then
// SHA-256(digest||key), the model the harness's element verifies.
func simavrActivateSig(img []byte) [tfw.OTAAuthSigLen]byte {
	tr := img[len(img)-128:]
	var m [32]byte
	copy(m[:], tr[16:48])
	return simavrSign(activateSigStructure(m, binary.LittleEndian.Uint32(tr[8:12]), 0))
}

// simavrConfirmSig is the row key's OTA_CONFIRM authorization for the image
// running on trial, the staged one: its trailer's measurement and version in
// the confirm Sig_structure, which tfw-boot verifies before it settles the
// trial (boot/boot_swap.c, docs/ota/api.md). The activate's never confirms.
func simavrConfirmSig(img []byte) [tfw.OTAAuthSigLen]byte {
	tr := img[len(img)-128:]
	var m [32]byte
	copy(m[:], tr[16:48])
	return simavrSign(confirmSigStructure(m, binary.LittleEndian.Uint32(tr[8:12])))
}

// simavrSign is element_sign's construction over a structure's SHA-256.
func simavrSign(structure []byte) [tfw.OTAAuthSigLen]byte {
	d := sha256.Sum256(structure)
	var sig [tfw.OTAAuthSigLen]byte
	a := sha256.Sum256(append(append([]byte{}, simavrElementKey[:]...), d[:]...))
	b := sha256.Sum256(append(append([]byte{}, d[:]...), simavrElementKey[:]...))
	copy(sig[:32], a[:])
	copy(sig[32:], b[:])
	return sig
}
