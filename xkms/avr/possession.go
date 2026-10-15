// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"encoding/asn1"
	"io"
	"math/big"
	"strconv"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/jeremyhahn/go-xkms/pkg/errs"
)

// PossessionSigner returns the crypto.Signer that proves possession of the
// attested key at ACME finalize: the device's ATTEST_CSR, for the nonce the
// attestation was made for.
//
// It is the finalize half of device-attest-01 for a DICE device. The challenge
// carried the alias certificate AttestKey produced for b =
// deviceattest01.DICENonce(keyAuthorization); the finalize CSR has to be
// signed by the same key over a request carrying the same b, and that is what
// this signer does. Hand it to deviceattest01.SignPossessionCSR -- or return it
// as the go-xkms ACME client's AttestedKey.Signer with Format FormatDICE, which
// calls that for you.
//
// IT SIGNS ONE THING. The device signs a CertificationRequestInfo it builds
// itself from its own identity and b; it never signs bytes the host sends,
// so the protocol stays free of a signing oracle. Sign asks the device for that
// signature and returns it only if it verifies over the digest it was asked
// to sign, under the attested key. Any other digest -- another document,
// another nonce, a request built for another identity -- is refused with
// ErrPossessionMismatch, never answered with a signature over something else.
//
// The public key is the alias certificate's subject key, which in this profile
// is the DeviceID key itself.
func (b *Backend) PossessionSigner(att *Attestation) (crypto.Signer, error) {
	if b.closed.Load() {
		return nil, ErrBackendClosed
	}
	if att == nil || att.Certificate == nil {
		return nil, &errs.Detail{Err: ErrInvalidAttestation, Detail: "no alias certificate"}
	}
	pub, ok := att.Certificate.PublicKey.(*ecdsa.PublicKey)
	if !ok || pub.Curve != elliptic.P256() {
		return nil, &errs.Detail{Err: ErrInvalidAttestation, Detail: "the attested key is not P-256"}
	}
	if len(att.Nonce) != tfw.AliasNonceLen {
		return nil, &errs.Detail{
			Err: ErrInvalidAttestation,
			Detail: "expected a " + strconv.Itoa(tfw.AliasNonceLen) +
				"-byte nonce, got " + strconv.Itoa(len(att.Nonce)),
		}
	}
	if !b.caps.Has(tfw.FeatureAttestCSR) {
		// The SDK refuses the same way before sending; refusing here means a
		// device that cannot finish an enrolment is found before the
		// challenge is answered rather than after.
		return nil, deviceError(tfw.StatusErrUnsupported)
	}

	s := &possessionSigner{backend: b, pub: pub}
	copy(s.nonce[:], att.Nonce)
	return s, nil
}

// possessionSigner is the crypto.Signer PossessionSigner returns.
type possessionSigner struct {
	backend *Backend
	pub     *ecdsa.PublicKey
	nonce   [tfw.AliasNonceLen]byte
}

// Public returns the attested key.
func (s *possessionSigner) Public() crypto.PublicKey { return s.pub }

// Sign returns the device's possession signature, DER-encoded, provided it is
// a signature over digest. digest must be SHA-256 of the CertificationRequestInfo
// the device builds for the signer's nonce; rand is unused, the element draws
// its own.
func (s *possessionSigner) Sign(_ io.Reader, digest []byte, opts crypto.SignerOpts) ([]byte, error) {
	b := s.backend
	if b.closed.Load() {
		return nil, ErrBackendClosed
	}
	if opts == nil || opts.HashFunc() != crypto.SHA256 || len(digest) != crypto.SHA256.Size() {
		return nil, &errs.Detail{
			Err:    ErrSigningUnsupported,
			Detail: "the possession request is signed over a SHA-256 digest only",
		}
	}

	b.wire.Lock()
	ctx, cancel := context.WithTimeout(context.Background(), b.config.RequestTimeout)
	raw, err := b.device.AttestCSR(ctx, s.nonce)
	cancel()
	b.wire.Unlock()
	if err != nil {
		return nil, deviceError(err)
	}

	half := len(raw) / 2
	r := new(big.Int).SetBytes(raw[:half])
	sig := new(big.Int).SetBytes(raw[half:])
	if !ecdsa.Verify(s.pub, digest, r, sig) {
		return nil, ErrPossessionMismatch
	}
	return asn1.Marshal(struct{ R, S *big.Int }{r, sig})
}
