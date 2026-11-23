// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

// Package tfmdice checks the DICE evidence the TF-M partition answers on the
// non-secure side (#608): the alias certificate the DeviceID key signed over a
// verifier nonce, and an alias-key signature over a verifier digest. The QEMU
// gate (test/integration/cortex-m/qemu_tfm_test.sh) runs it through
// cmd/tfm-dice-check on what the image printed.
//
// The nonce is read with go-xkms's dice package (the eat_nonce claim of the
// tcg-dice-UCCS-evidence extension), the same reader device-attest-01 uses.
package tfmdice

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"crypto/x509"
	"errors"
	"fmt"
	"math/big"

	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
)

// Lengths of the partition's wire values.
const (
	PubLen    = 64 // P-256 X||Y
	NonceLen  = 32
	DigestLen = 32
	SigLen    = 64 // r||s
)

// Input is what the non-secure check printed and what it asked with.
type Input struct {
	DeviceIDPub []byte // tfw_s_dice_deviceid_pub, X||Y
	AliasPub    []byte // tfw_s_dice_alias_pub, X||Y
	CertDER     []byte // tfw_s_dice_alias_cert over Nonce, all pages
	Nonce       []byte // the nonce the certificate was asked over
	Digest      []byte // the digest tfw_s_dice_alias_sign was asked over
	Sig         []byte // tfw_s_dice_alias_sign's r||s
}

func p256(name string, xy []byte) (*ecdsa.PublicKey, error) {
	if len(xy) != PubLen {
		return nil, fmt.Errorf("tfmdice: %s public key: %d bytes, want %d", name, len(xy), PubLen)
	}
	k, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{4}, xy...))
	if err != nil {
		return nil, fmt.Errorf("tfmdice: %s public key: %w", name, err)
	}
	return k, nil
}

// Check returns nil when the evidence holds, else an error naming the check
// that failed.
func Check(in Input) error {
	dev, err := p256("deviceid", in.DeviceIDPub)
	if err != nil {
		return err
	}
	alias, err := p256("alias", in.AliasPub)
	if err != nil {
		return err
	}
	if len(in.Nonce) != NonceLen {
		return fmt.Errorf("tfmdice: nonce: %d bytes, want %d", len(in.Nonce), NonceLen)
	}
	if len(in.Digest) != DigestLen {
		return fmt.Errorf("tfmdice: digest: %d bytes, want %d", len(in.Digest), DigestLen)
	}

	cert, err := x509.ParseCertificate(in.CertDER)
	if err != nil {
		return fmt.Errorf("tfmdice: alias certificate: %w", err)
	}
	if cert.SignatureAlgorithm != x509.ECDSAWithSHA256 {
		return fmt.Errorf("tfmdice: alias certificate signature: algorithm %v, want ECDSA-SHA256", cert.SignatureAlgorithm)
	}
	tbs := sha256.Sum256(cert.RawTBSCertificate)
	if !ecdsa.VerifyASN1(dev, tbs[:], cert.Signature) {
		return errors.New("tfmdice: alias certificate signature: not by the DeviceID key")
	}
	subject, ok := cert.PublicKey.(*ecdsa.PublicKey)
	if !ok || !subject.Equal(alias) {
		return errors.New("tfmdice: alias public key: the certificate certifies another key")
	}

	got, err := dice.Challenge(cert)
	if err != nil {
		return fmt.Errorf("tfmdice: nonce: %w", err)
	}
	if !bytes.Equal(got, in.Nonce) {
		return fmt.Errorf("tfmdice: nonce: certificate carries %x, want %x", got, in.Nonce)
	}

	if len(in.Sig) != SigLen {
		return fmt.Errorf("tfmdice: alias signature: %d bytes, want %d", len(in.Sig), SigLen)
	}
	r := new(big.Int).SetBytes(in.Sig[:SigLen/2])
	s := new(big.Int).SetBytes(in.Sig[SigLen/2:])
	if !ecdsa.Verify(alias, in.Digest, r, s) {
		return errors.New("tfmdice: alias signature: does not verify with the alias key")
	}
	return nil
}
