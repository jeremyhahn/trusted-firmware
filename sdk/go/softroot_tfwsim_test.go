// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package tfw

import (
	"bytes"
	"context"
	"crypto/ecdh"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"math/big"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/simproc"
)

// The soft root's P-256 DeviceID end to end (#48, #130): the CRYPTO=auto image
// (tfwsim-auto) on a board with no element, which falls back to the soft root,
// with a soft tfw-boot holding a UDS handing it the DeviceID seed, through this
// SDK. A CRYPTO=soft image has no ATTEST_CERT. Offsets into the evidence frame
// are include/tfw/protocol.h's.
const (
	softEvidenceAlg = 73
	softEvidenceSig = 74
)

var (
	softUDS      = bytes.Repeat([]byte{0x5c}, 32)
	softOtherUDS = bytes.Repeat([]byte{0xa3}, 32)
)

func startSoftRoot(t *testing.T, uds []byte) *Device {
	t.Helper()
	return New(simproc.Start(t, simproc.Auto, "--no-element", "--uds", hex.EncodeToString(uds)), WithTimeout(5*time.Second))
}

// softDeviceID is the subject key of the alias certificate: the soft root has
// no separate Alias key, so it is the DeviceID's public half.
func softDeviceID(t *testing.T, d *Device, nonce byte) *ecdsa.PublicKey {
	t.Helper()
	der, err := d.AttestCert(context.Background(), bytes.Repeat([]byte{nonce}, AliasNonceLen))
	require.NoError(t, err, "ATTEST_CERT on the provisioned soft root")
	cert, err := x509.ParseCertificate(der)
	require.NoError(t, err)
	pub, ok := cert.PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok, "subject key is %T, want *ecdsa.PublicKey", cert.PublicKey)
	require.Equal(t, x509.ECDSAWithSHA256, cert.SignatureAlgorithm)
	require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature),
		"the certificate does not verify under its own subject key")
	return pub
}

func TestSoftRootCapsReportsES256AndNotHMAC(t *testing.T) {
	c, err := startSoftRoot(t, softUDS).Caps(context.Background())
	require.NoError(t, err)
	assert.Equal(t, CryptoSoftware, c.CryptoTier)
	assert.Equal(t, AlgECDSAP256SHA256, c.Algorithms, "algorithms = %s, want ecdsa-p256-sha256 alone", c.Algorithms)
	assert.False(t, c.Algorithms.Can(AlgHMACSHA256), "the P-256 soft root still claims HMAC")
}

func TestSoftRootPublicKeyIsAnUncompressedP256Point(t *testing.T) {
	pub := softDeviceID(t, startSoftRoot(t, softUDS), 0x11)
	require.Equal(t, elliptic.P256(), pub.Curve)

	// crypto/ecdh refuses a point off the curve or at infinity.
	raw, err := pub.Bytes()
	require.NoError(t, err)
	require.Len(t, raw, 65)
	assert.EqualValues(t, 0x04, raw[0], "uncompressed SEC 1 point")
	_, err = ecdh.P256().NewPublicKey(raw)
	require.NoError(t, err, "the DeviceID is not a point on P-256")

	// One UDS, one key: a second boot derives the same point, a second UDS
	// another.
	assert.True(t, pub.Equal(softDeviceID(t, startSoftRoot(t, softUDS), 0x22)), "the DeviceID moved between boots")
	assert.False(t, pub.Equal(softDeviceID(t, startSoftRoot(t, softOtherUDS), 0x11)), "two UDS gave one DeviceID")
}

func TestSoftRootSignatureVerifiesES256UnderTheDeviceID(t *testing.T) {
	d := startSoftRoot(t, softUDS)
	pub := softDeviceID(t, d, 0x33)

	ev, err := d.Attest(context.Background(), bytes.Repeat([]byte{0x44}, AttestNonceLen))
	require.NoError(t, err)
	require.Len(t, ev, EvidenceLen)
	// ES256 as the element signs, so the AlgID names the root instead (#130).
	assert.EqualValues(t, 0x11, ev[softEvidenceAlg], "alg id, want TFW_ALG_ECDSA_P256_SHA256_SOFT")

	// Raw R||S over the SHA-256 of everything before the algorithm byte.
	digest := sha256.Sum256(ev[:softEvidenceAlg])
	r := new(big.Int).SetBytes(ev[softEvidenceSig : softEvidenceSig+32])
	s := new(big.Int).SetBytes(ev[softEvidenceSig+32 : softEvidenceSig+64])
	require.True(t, ecdsa.Verify(pub, digest[:], r, s), "the evidence signature does not verify under the DeviceID")

	other := softDeviceID(t, startSoftRoot(t, softOtherUDS), 0x33)
	assert.False(t, ecdsa.Verify(other, digest[:], r, s), "the signature verifies under another device's key")
	ev[1] ^= 0x01
	tampered := sha256.Sum256(ev[:softEvidenceAlg])
	assert.False(t, ecdsa.Verify(pub, tampered[:], r, s), "the signature verifies over a tampered frame")
}
