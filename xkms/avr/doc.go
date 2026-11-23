// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of go-xkms.
//
// go-xkms is dual-licensed:
//
// 1. GNU Affero General Public License v3.0 (AGPL-3.0)
//    See LICENSE file or visit https://www.gnu.org/licenses/agpl-3.0.html
//
// 2. Commercial License
//    Contact licensing@automatethethings.com for commercial licensing options.

// Package avr makes a classic AVR device a first-class xkms backend.
//
// The device is reached over the trusted-firmware wire protocol through that
// project's Layer 0 host SDK, github.com/jeremyhahn/trusted-firmware/sdk/go.
// trusted-firmware owns the protocol, the firmware that speaks it and the host
// SDK for it; this package owns none of those and reimplements none of them.
// Every byte that goes on the wire is put there by an SDK call.
//
// # What the device is
//
// A 16 MHz part with a secure element on its I2C bus. Its identity keypair is
// generated inside the element on a provisioning jig and the private half never
// leaves it: there is no command on the wire that exports it, and none that
// signs a digest a host supplied. This backend therefore holds no private key
// and needs no locked memory — it carries public points and what the device
// says about itself.
//
// # What this backend does and does not do
//
// It reports, truthfully and from the device's own CAPS reply:
//
//   - the attestation level, derived from Caps.CryptoTier and nothing else;
//   - the signature algorithms the element can actually perform, derived from
//     Caps.Algorithms and nothing else;
//   - the device's identity public point, read back from the element's slot by
//     a call that recomputes it and generates nothing;
//   - the element's serial number and the lock state of both zones, on a
//     provisioning image.
//
// A device record is keyed on the identity public key rather than on the
// element serial, because the serial is readable only from a provisioning
// image — those commands are compiled out of the firmware a part ships with,
// on purpose. Backend.Identity is the bench-time cross-check that the key
// belongs to the element it should.
//
// It refuses, with a typed error naming the reason, every operation the device
// cannot perform: key generation, signing, decryption, deletion and rotation.
// A refusal is the answer, not a placeholder — see Backend.Signer for why the
// wire deliberately has no general signing command.
//
// # Attestation
//
// Backend.AttestKey asks the device to mint an alias certificate for a
// challenge — ATTEST_CERT, which the SDK pages internally — and checks what
// came back: that it parses, that its key is on P-256 and its signature is
// ECDSA-SHA256 (the element can produce nothing else), that its
// tcg-dice-UCCS-evidence extension is readable, and that the eat_nonce claim
// in it is the challenge this host issued. The nonce check is unconditional
// and compared in constant time, because a DICE device has no clock and that
// nonce is the only thing separating a live answer from a replayed recording.
//
// The challenge is 32 bytes and is SHA-256 of the ACME key authorization.
// Compute it with deviceattest01.DICENonce. This package does not hash: a
// caller passing a raw key authorization is refused for its width rather than
// quietly enrolled against something else. The device's OTHER nonce — the 16
// bytes riding in the evidence frame — is runtime freshness and is a different
// value of a different width.
//
// # How AttestKey verifies
//
// AttestKey parses the alias certificate and makes one dice.Verify call with
// the issued nonce, the configured roots and RequireDeviceSignedLeaf. The
// decoded dice.ClaimsSet and UEID it returns are handed to the caller in
// Attestation, the UEID and the firmware measurement being CWT claims 256 and
// 273 inside the tcg-dice-UCCS-evidence extension. Nothing about the DICE
// formats is re-implemented here. An earlier open-coded path was removed once
// go-xkms's dice accepted UCCS in place of DiceTcbInfo.
package avr
