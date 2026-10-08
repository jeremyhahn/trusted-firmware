// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"encoding/binary"
)

// Attestation commands and the two nonce widths.
//
// These exist because a consumer that had to page the wire itself would be a
// consumer holding a second copy of this protocol — and the one place a wire
// format may live is the project that owns it. go-xkms needed exactly this and
// correctly refused to reimplement it rather than declaring the command ids and
// page sizes on its own side.
// CmdAttest is declared in device.go with the rest of the command ids.
const CmdAttestCert Cmd = 0x63

const (
	// AttestNonceLen is RUNTIME FRESHNESS: the nonce carried in the 164-byte
	// evidence frame, answering "is this device reporting now".
	AttestNonceLen = 16

	// AliasNonceLen is ENROLMENT: the nonce carried in the eat_nonce claim of
	// the alias certificate, answering "is this key on this device".
	//
	// These are deliberately different widths and merging them is a wire break.
	// ACME device-attest-01 compares the attestation nonce against the key
	// authorization — "<token>.<RFC 7638 thumbprint>", 87 bytes, since the
	// thumbprint alone is 43 base64url characters — and RFC 9711 bounds
	// eat_nonce at 8..64, so the raw form cannot be carried by a conforming
	// claims set at all. The profile carries SHA-256 of the key authorization
	// instead, which is 32 bytes. See DICENonce on the verifier side.
	AliasNonceLen = 32

	// EvidenceLen is the size of one complete attestation frame.
	EvidenceLen = 164

	attestPage = 48 // TFW_ATTEST_PAGE
	attestHdr  = 2  // [page:1][total:1] ahead of each ATTEST page
	aliasPage  = 60 // TFW_ALIAS_PAGE
)

// Attest asks the device for nonce-bound attestation evidence.
//
// The device computes the evidence when it receives the nonce and returns it a
// page at a time; this pages internally so a caller sees one call and 164
// bytes. The pages are REGENERATED rather than cached, so they are consistent
// only until the next nonce arrives — no part in this family has room to hold
// the document — which is why this must not be called concurrently on one
// device.
func (d *Device) Attest(ctx context.Context, nonce []byte) ([]byte, error) {
	if len(nonce) != AttestNonceLen {
		return nil, &ArgRangeError{
			Cmd: CmdAttest, Arg: "nonce", Value: int64(len(nonce)),
			Min: AttestNonceLen, Max: AttestNonceLen, Unit: "bytes",
		}
	}

	out := make([]byte, 0, EvidenceLen)
	f, err := d.Do(ctx, CmdAttest, nonce)
	if err != nil {
		return nil, err
	}
	body, err := attestPageBody(f.Payload)
	if err != nil {
		return nil, err
	}
	out = append(out, body...)

	for page := byte(1); len(out) < EvidenceLen; page++ {
		f, err = d.Do(ctx, CmdAttest, []byte{page})
		if err != nil {
			return nil, err
		}
		// A page with no evidence in it would loop forever. The device answers
		// TFW_ERR_RANGE past the end rather than an empty success, so reaching
		// here means the two ends disagree about the length.
		if body, err = attestPageBody(f.Payload); err != nil {
			return nil, err
		}
		out = append(out, body...)
	}
	return out[:EvidenceLen], nil
}

// attestPageBody strips the [page:1][total:1] header every ATTEST reply
// carries (src/caps/attest.c) and returns the evidence bytes behind it. Without
// this the headers land in the middle of the evidence: page 0's total byte
// shifts every field by two and each later page splices two more bytes in, so
// the frame comes back the right length with the wrong contents.
func attestPageBody(payload []byte) ([]byte, error) {
	if len(payload) <= attestHdr {
		return nil, &ShortReplyError{Cmd: CmdAttest, Got: len(payload), Need: attestHdr + 1}
	}
	return payload[attestHdr:], nil
}

// AttestCert asks the device to mint an alias certificate carrying the
// challenge, and returns it as DER.
//
// This is the DICE path. The certificate holds the nonce in the eat_nonce claim
// of its tcg-dice-UCCS-evidence extension, and the certificate's own signature
// is what protects that extension — the extension carries no signature of its
// own, per TCG DICE Attestation Architecture r23 §6.1.4.
//
// The device mints and signs it, rather than signing a structure a host built.
// That is the point: a device that signed a host's bytes could not know what it
// had attested to.
//
// Returns ErrUnsupported wrapped in a device error when the build has no
// element to certify — a software-tier device knows exactly what a certificate
// is and has no public half to put in one.
func (d *Device) AttestCert(ctx context.Context, nonce []byte) ([]byte, error) {
	if len(nonce) != AliasNonceLen {
		return nil, &ArgRangeError{
			Cmd: CmdAttestCert, Arg: "nonce", Value: int64(len(nonce)),
			Min: AliasNonceLen, Max: AliasNonceLen, Unit: "bytes",
		}
	}

	f, err := d.Do(ctx, CmdAttestCert, nonce)
	if err != nil {
		return nil, err
	}
	// [page:1][total:2 little-endian][bytes]. The total is two bytes because a
	// certificate is longer than one byte can count.
	const hdr = 3
	if len(f.Payload) < hdr {
		return nil, &ShortReplyError{Cmd: CmdAttestCert, Got: len(f.Payload), Need: hdr}
	}
	total := int(binary.LittleEndian.Uint16(f.Payload[1:3]))
	if total == 0 {
		return nil, &ShortReplyError{Cmd: CmdAttestCert, Got: 0, Need: 1}
	}

	out := make([]byte, 0, total)
	out = append(out, f.Payload[hdr:]...)

	for page := byte(1); len(out) < total; page++ {
		f, err = d.Do(ctx, CmdAttestCert, []byte{page})
		if err != nil {
			return nil, err
		}
		if len(f.Payload) <= hdr {
			return nil, &ShortReplyError{Cmd: CmdAttestCert, Got: len(out), Need: total}
		}
		out = append(out, f.Payload[hdr:]...)
	}
	return out[:total], nil
}
