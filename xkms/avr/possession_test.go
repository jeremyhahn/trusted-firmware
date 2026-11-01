// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"bytes"
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// possessionLink is a secure-element device that reports ATTEST_CSR and
// answers it with the element key's signature over SHA-256(cri): what the
// device does for the request it builds itself.
func possessionLink(t *testing.T, key *ecdsa.PrivateKey, cri []byte) *fakeLink {
	t.Helper()
	digest := sha256.Sum256(cri)
	r, s, err := ecdsa.Sign(rand.Reader, key, digest[:])
	require.NoError(t, err)
	sig := make([]byte, tfw.CSRSigLen)
	r.FillBytes(sig[:tfw.CSRSigLen/2])
	s.FillBytes(sig[tfw.CSRSigLen/2:])

	return newFakeLink().
		answer(tfw.CmdCaps, capsPayload(
			tfw.CryptoSecureElement,
			tfw.FeatureAttest|tfw.FeatureAliasCert|tfw.FeatureAttestCSR,
			tfw.AlgECDSAP256SHA256,
		)).
		answer(tfw.CmdAttestCSR, sig)
}

func possessionAttestation(t *testing.T, key *ecdsa.PrivateKey, nonce []byte) *Attestation {
	t.Helper()
	return &Attestation{
		Format:      FormatDICE,
		Certificate: &x509.Certificate{PublicKey: &key.PublicKey},
		Nonce:       nonce,
	}
}

func testPossessionKey(t *testing.T) *ecdsa.PrivateKey {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	return key
}

var possessionNonce = bytes.Repeat([]byte{0xB5}, tfw.AliasNonceLen)

func TestPossessionSignerSignsTheDevicesRequest(t *testing.T) {
	key := testPossessionKey(t)
	cri := []byte("the CertificationRequestInfo the device builds")
	link := possessionLink(t, key, cri)
	b := newTestBackend(t, link)

	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)
	assert.Equal(t, &key.PublicKey, signer.Public())

	digest := sha256.Sum256(cri)
	der, err := signer.Sign(rand.Reader, digest[:], crypto.SHA256)
	require.NoError(t, err)
	assert.True(t, ecdsa.VerifyASN1(&key.PublicKey, digest[:], der),
		"the signature is a DER ECDSA-Sig-Value over the digest")

	// What crossed the wire is the nonce and nothing else: the host chooses
	// b, never the bytes signed.
	last := link.requests[len(link.requests)-1]
	assert.Equal(t, tfw.CmdAttestCSR, last.Cmd())
	assert.Equal(t, possessionNonce, last.Payload)
}

// The device signs its own request whatever digest the caller hands over. A
// signature over something else is refused, never returned: this is the
// refusal a request built for another nonce or another identity meets.
func TestPossessionSignerRefusesADigestTheDeviceDidNotSign(t *testing.T) {
	key := testPossessionKey(t)
	b := newTestBackend(t, possessionLink(t, key, []byte("the device's request")))

	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)

	other := sha256.Sum256([]byte("a request for another nonce"))
	der, err := signer.Sign(rand.Reader, other[:], crypto.SHA256)

	assert.Nil(t, der)
	assert.ErrorIs(t, err, ErrPossessionMismatch)
}

// A signature by a key other than the attested one is refused as well: the
// attestation names the key, not whatever the device happens to answer with.
func TestPossessionSignerRefusesAnotherKeysSignature(t *testing.T) {
	deviceKey, attestedKey := testPossessionKey(t), testPossessionKey(t)
	cri := []byte("the device's request")
	b := newTestBackend(t, possessionLink(t, deviceKey, cri))

	signer, err := b.PossessionSigner(possessionAttestation(t, attestedKey, possessionNonce))
	require.NoError(t, err)

	digest := sha256.Sum256(cri)
	_, err = signer.Sign(rand.Reader, digest[:], crypto.SHA256)

	assert.ErrorIs(t, err, ErrPossessionMismatch)
}

func TestPossessionSignerSignsSHA256Only(t *testing.T) {
	key := testPossessionKey(t)
	link := possessionLink(t, key, []byte("cri"))
	b := newTestBackend(t, link)
	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)

	before := len(link.requests)
	for name, call := range map[string]func() ([]byte, error){
		"sha384":   func() ([]byte, error) { return signer.Sign(nil, make([]byte, 48), crypto.SHA384) },
		"nil opts": func() ([]byte, error) { return signer.Sign(nil, make([]byte, 32), nil) },
		"short":    func() ([]byte, error) { return signer.Sign(nil, make([]byte, 20), crypto.SHA256) },
		// Another hash of the same width: only the hash, not the length, refuses it.
		"sha512/256": func() ([]byte, error) { return signer.Sign(nil, make([]byte, 32), crypto.SHA512_256) },
	} {
		_, err := call()
		assert.ErrorIs(t, err, ErrSigningUnsupported, name)
	}
	assert.Len(t, link.requests, before, "a refused digest costs no round trip")
}

func TestPossessionSignerRefusesWhatAttestKeyDidNotProduce(t *testing.T) {
	key := testPossessionKey(t)
	b := newTestBackend(t, possessionLink(t, key, []byte("cri")))

	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	require.NoError(t, err)

	for name, att := range map[string]*Attestation{
		"nil":            nil,
		"no certificate": {Nonce: possessionNonce},
		"not P-256":      possessionAttestation(t, p384, possessionNonce),
		"evidence nonce": possessionAttestation(t, key, make([]byte, tfw.AttestNonceLen)),
		"no nonce":       possessionAttestation(t, key, nil),
		"long nonce":     possessionAttestation(t, key, make([]byte, 2*tfw.AliasNonceLen)),
	} {
		t.Run(name, func(t *testing.T) {
			signer, err := b.PossessionSigner(att)

			assert.Nil(t, signer)
			assert.ErrorIs(t, err, ErrInvalidAttestation)
		})
	}
}

// A device whose CAPS lack ATTEST_CSR cannot finish an enrolment, and says so
// before the challenge is answered rather than at finalize.
func TestPossessionSignerRefusesADeviceWithoutAttestCSR(t *testing.T) {
	key := testPossessionKey(t)
	link := secureElementLink()
	b := newTestBackend(t, link)

	before := len(link.requests)
	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))

	assert.Nil(t, signer)
	assert.ErrorIs(t, err, ErrUnsupportedByDevice)
	assert.Len(t, link.requests, before)
}

func TestPossessionSignerPassesDeviceRefusalsThrough(t *testing.T) {
	key := testPossessionKey(t)
	link := possessionLink(t, key, []byte("cri")).refuse(tfw.CmdAttestCSR, tfw.StatusErrNotProvisioned)
	b := newTestBackend(t, link)
	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)

	digest := sha256.Sum256([]byte("cri"))
	_, err = signer.Sign(rand.Reader, digest[:], crypto.SHA256)

	assert.ErrorIs(t, err, ErrNotProvisioned)
}

func TestPossessionSignerRefusesAfterClose(t *testing.T) {
	key := testPossessionKey(t)
	link := possessionLink(t, key, []byte("cri"))
	b, err := NewBackend(context.Background(), testConfig(), newTestDevice(link))
	require.NoError(t, err)
	signer, err := b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	require.NoError(t, err)
	require.NoError(t, b.Close())

	before := len(link.requests)
	digest := sha256.Sum256([]byte("cri"))
	_, err = signer.Sign(rand.Reader, digest[:], crypto.SHA256)
	assert.ErrorIs(t, err, ErrBackendClosed)
	assert.Len(t, link.requests, before)

	_, err = b.PossessionSigner(possessionAttestation(t, key, possessionNonce))
	assert.ErrorIs(t, err, ErrBackendClosed)
}
