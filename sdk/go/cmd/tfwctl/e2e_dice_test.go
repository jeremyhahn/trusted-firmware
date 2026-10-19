// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

//go:build tfwsim

package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// These scenarios drive tfwctl's attest, attest-cert and caps verbs against the
// element-tier simulator: the real firmware, the real ATECC608A driver and
// provider, and the software model of the part as the I2C bus. They check the
// DICE evidence the CLI prints -- its structure, its identity claims, its nonce
// binding and, because the model does real P-256, its SIGNATURES, with
// crypto/ecdsa and crypto/x509 and nothing from this repository's C.
//
// The key being verified against is the documented TEST key, pinned as a
// literal in tfwsim.TestDeviceIDPublicKey. It proves the evidence is well formed
// and signed; it says nothing about any device, and no peer may trust it.
//
// The SDK's own refusal of a wrong-width nonce is a unit test:
// sdk/go/attest_test.go TestAttestRefusesTheWrongNonceWidth. The frame
// offsets, the CBOR reader and verifyFrame are in e2e_helpers_test.go.

func TestE2EAttestCertIsDICEEvidenceBoundToTheCallersNonce(t *testing.T) {
	nonce := aliasNonce(0x40)
	cert := parseCert(t, cliAttestCert(t, tfwsim.StartElement(t), nonce))
	gotNonce, ueid, fwHash := claimsOf(t, uccsOf(t, cert))

	assert.Equal(t, nonce, gotNonce, "eat_nonce is the caller's")
	// RFC 9711 section 4.2.1.1: a leading type byte, 0x01 for a random identifier.
	assert.Equal(t, append([]byte{0x01}, tfwsim.TestSerial[:]...), ueid, "ueid")
	assert.Equal(t, tfwsim.TestFWHash[:], fwHash, "FWHash")
}

// cliAttestCertJSON runs attest-cert --json and returns the certificate and the
// app_evidence field beside it: the 32 bytes of the certificate's measurements
// claim (273).
func cliAttestCertJSON(t *testing.T, dev *tfw.Device, nonce []byte) (*x509.Certificate, []byte) {
	t.Helper()
	var got struct {
		Certificate string `json:"certificate"`
		Len         int    `json:"len"`
		AppEvidence string `json:"app_evidence"`
	}
	cliJSON(t, dev, &got, "attest-cert", hex.EncodeToString(nonce))
	der, err := hex.DecodeString(got.Certificate)
	require.NoError(t, err)
	require.Len(t, der, got.Len, "attest-cert --json len")
	ev, err := hex.DecodeString(got.AppEvidence)
	require.NoError(t, err, "app_evidence is not hex: %q", got.AppEvidence)
	return parseCert(t, der), ev
}

// #51: a running application's 32 bytes are claim 273's single hash value in
// the certificate the DeviceID key signs, and tfwctl prints them as
// app_evidence. test/app/example supplies vectors.AppEvExample.
func TestE2EAttestCertCarriesTheApplicationsEvidence(t *testing.T) {
	d := tfwsim.StartElementApp(t)
	nonce := aliasNonce(0x51)
	cert, ev := cliAttestCertJSON(t, d, nonce)

	assert.Equal(t, []byte(vectors.AppEvExample), ev, "app_evidence is the application's 32 bytes")
	gotNonce, ueid, measured := claimsOf(t, uccsOf(t, cert))
	assert.Equal(t, []byte(vectors.AppEvExample), measured, "claim 273 carries the application's evidence")
	assert.Equal(t, nonce, gotNonce, "eat_nonce is the caller's")
	assert.Equal(t, append([]byte{0x01}, tfwsim.TestSerial[:]...), ueid, "ueid")
	require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature),
		"the certificate carrying the application's evidence does not verify")
	tbs := sha256.Sum256(cert.RawTBSCertificate)
	assert.True(t, ecdsa.VerifyASN1(tfwsim.TestDeviceIDPublicKey(), tbs[:], cert.Signature),
		"the application's evidence is not signed by the DeviceID key")

	// The application's bytes take claim 273 only: the evidence frame still
	// reports the firmware measurement.
	frame := cliAttest(t, d, attestNonce(0x51))
	assert.Equal(t, tfwsim.TestFWHash[:], frame[offFWHash:offFWHash+32], "the frame's FWHash moved with the application")
}

// With no application, claim 273 keeps the firmware measurement: go-xkms's
// possession template has no place for a claims set without it, and the
// firmware never fabricates the application's bytes.
func TestE2EAttestCertWithoutAnApplicationCarriesTheFirmwareMeasurement(t *testing.T) {
	cert, ev := cliAttestCertJSON(t, tfwsim.StartElement(t), aliasNonce(0x52))
	assert.Equal(t, tfwsim.TestFWHash[:], ev, "app_evidence")
	_, _, measured := claimsOf(t, uccsOf(t, cert))
	assert.Equal(t, tfwsim.TestFWHash[:], measured, "claim 273")
}

// THE POINT OF THE REAL CURVE: the alias certificate's signature verifies, under
// the DeviceID public key, with the standard library.
func TestE2EAttestCertSignatureVerifiesUnderTheDeviceIDKey(t *testing.T) {
	cert := parseCert(t, cliAttestCert(t, tfwsim.StartElement(t), aliasNonce(0x40)))
	deviceID := tfwsim.TestDeviceIDPublicKey()

	// The subject key IS the DeviceID key (docs/dice-attestation: there is no
	// separately derived Alias key), and it is the documented one.
	subject, ok := cert.PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok && subject.Equal(deviceID), "subject public key = %v, want the DeviceID test key", cert.PublicKey)
	require.Equal(t, x509.ECDSAWithSHA256, cert.SignatureAlgorithm)

	// Both ways: the library's own check, and the digest worked out by hand, so a
	// quirk in either does not hide a wrong signature.
	require.NoError(t, cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature))
	tbs := sha256.Sum256(cert.RawTBSCertificate)
	assert.True(t, ecdsa.VerifyASN1(deviceID, tbs[:], cert.Signature),
		"ecdsa.VerifyASN1 rejects the certificate signature under the DeviceID key")
}

func TestE2EAttestCertSignatureRejectsTamperingAndOtherKeys(t *testing.T) {
	cert := parseCert(t, cliAttestCert(t, tfwsim.StartElement(t), aliasNonce(0x40)))

	// One flipped bit anywhere in the signed bytes.
	tampered := append([]byte(nil), cert.RawTBSCertificate...)
	tampered[len(tampered)/2] ^= 0x01
	assert.Error(t, cert.CheckSignature(cert.SignatureAlgorithm, tampered, cert.Signature),
		"a certificate with a modified TBS still verified")

	// A key that is not the device's.
	other, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	tbs := sha256.Sum256(cert.RawTBSCertificate)
	assert.False(t, ecdsa.VerifyASN1(&other.PublicKey, tbs[:], cert.Signature),
		"the certificate verified under an unrelated key")
}

func TestE2EAttestCertDiffersWithTheNonce(t *testing.T) {
	d := tfwsim.StartElement(t)
	a := cliAttestCert(t, d, aliasNonce(0x10))
	b := cliAttestCert(t, d, aliasNonce(0x11))
	require.NotEqual(t, a, b, "two different nonces produced the same certificate")
	ca, cb := parseCert(t, a), parseCert(t, b)

	// The signature is over a digest that covers the nonce, so it must move too.
	assert.NotEqual(t, ca.Signature, cb.Signature, "the certificate signature did not change with the nonce")
	// ...and each is valid for ITS nonce only: a's signature does not verify
	// over b's TBS.
	assert.Error(t, ca.CheckSignature(ca.SignatureAlgorithm, cb.RawTBSCertificate, ca.Signature),
		"a signature verified over a certificate bound to a different nonce")

	na, _, _ := claimsOf(t, uccsOf(t, ca))
	nb, _, _ := claimsOf(t, uccsOf(t, cb))
	assert.NotEqual(t, na, nb, "eat_nonce did not change with the nonce")
}

func TestE2EAttestCertSameNonceIsReproducible(t *testing.T) {
	// A simulator is a test oracle only if the same input gives the same
	// output; the certificate carries no clock.
	nonce := aliasNonce(0x77)
	first := cliAttestCert(t, tfwsim.StartElement(t), nonce)
	second := cliAttestCert(t, tfwsim.StartElement(t), nonce)
	assert.Equal(t, first, second, "the same nonce produced different certificates on a fresh simulator")
}

func TestE2EAttestEvidenceFrame(t *testing.T) {
	d := tfwsim.StartElement(t)
	nonce := attestNonce(0x5a)

	ev := cliAttest(t, d, nonce)
	require.Len(t, ev, tfw.EvidenceLen)
	assert.Equal(t, nonce, ev[offNonce:offNonce+tfw.AttestNonceLen], "frame nonce")
	assert.Equal(t, tfwsim.TestSerial[:], ev[offDevice:offDevice+tfw.SerialLen], "DeviceID")
	assert.Equal(t, tfwsim.TestFWHash[:], ev[offFWHash:offFWHash+32], "FWHash")
	assert.EqualValues(t, algECDSA, ev[offAlg], "algorithm is ECDSA P-256")
	assert.NotEqual(t, make([]byte, 64), ev[offSig:offSig+64], "signature field is empty")
	assert.True(t, verifyFrame(tfwsim.TestDeviceIDPublicKey(), ev),
		"the evidence signature does not verify under the DeviceID key")

	other := cliAttest(t, d, attestNonce(0x5b))
	assert.NotEqual(t, ev, other, "two different nonces produced identical evidence")
	assert.NotEqual(t, ev[offSig:offSig+64], other[offSig:offSig+64], "the evidence signature did not change with the nonce")
}

func TestE2EEvidenceFrameRejectsTamperingAndOtherKeys(t *testing.T) {
	ev := cliAttest(t, tfwsim.StartElement(t), attestNonce(0x5a))
	pub := tfwsim.TestDeviceIDPublicKey()
	require.True(t, verifyFrame(pub, ev), "the untouched frame does not verify")

	// Every field the signature covers: nonce, DeviceID, FWHash. Flipping a bit
	// in any of them must break it, or the signature is not binding that field.
	for name, off := range map[string]int{
		"version": offVersion, "nonce": offNonce, "device id": offDevice, "fw hash": offFWHash,
	} {
		bad := append([]byte(nil), ev...)
		bad[off] ^= 0x01
		assert.False(t, verifyFrame(pub, bad), "a frame with a modified %s still verified", name)
	}

	bad := append([]byte(nil), ev...)
	bad[offSig+7] ^= 0x01
	assert.False(t, verifyFrame(pub, bad), "a frame with a modified signature still verified")

	other, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	assert.False(t, verifyFrame(&other.PublicKey, ev), "the frame verified under an unrelated key")
}

// docs/dice-attestation: the DeviceID key signs both artefacts, and the
// certificate names it as its subject, so one public key checks the frame and
// the certificate alike. A peer pinning one key per device needs exactly this.
func TestE2EEvidenceAndCertificateShareOneKey(t *testing.T) {
	d := tfwsim.StartElement(t)
	ev := cliAttest(t, d, attestNonce(0x33))
	subject, ok := parseCert(t, cliAttestCert(t, d, aliasNonce(0x33))).PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok, "certificate subject is not an ECDSA key")
	assert.True(t, verifyFrame(subject, ev), "the evidence frame does not verify under the certificate's subject key")
}

func TestE2EElementReportsItsRootOfTrust(t *testing.T) {
	c := cliCaps(t, tfwsim.StartElement(t))
	assert.Equal(t, tfw.CryptoSecureElement, c.CryptoTier)
	assert.True(t, c.Algorithms.Can(tfw.AlgECDSAP256SHA256), "algorithms = %v, want ECDSA P-256", c.Algorithms)
	assert.True(t, c.Has(tfw.FeatureAttest) && c.Has(tfw.FeatureAliasCert), "features %#x lack attest/alias cert", c.Features)
}

func TestE2EBlankElementRefusesToAttest(t *testing.T) {
	d := tfwsim.StartBlankElement(t)

	// The element is present, so the tier is honest about it...
	assert.Equal(t, tfw.CryptoSecureElement, cliCaps(t, d).CryptoTier)
	// ...and with no identity in it, evidence must be refused, not faked.
	var st tfw.Status
	_, err := cliHex(t, d, "attest", "evidence", attestNonce(1))
	require.True(t, errorsAsStatus(err, &st), "attest on a blank element: %v", err)
	assert.Equal(t, tfw.StatusErrNotProvisioned, st)
	_, err = cliHex(t, d, "attest-cert", "certificate", aliasNonce(1))
	require.True(t, errorsAsStatus(err, &st), "attest-cert on a blank element: %v", err)
	assert.Equal(t, tfw.StatusErrNotProvisioned, st)
}

func TestE2ESoftwareTierStillCannotIssueACertificate(t *testing.T) {
	// The reason the element tier exists: the default tfwsim has no asymmetric key.
	_, err := cliHex(t, tfwsim.Start(t), "attest-cert", "certificate", aliasNonce(1))
	var st tfw.Status
	require.True(t, errorsAsStatus(err, &st), "attest-cert on the software tier: %v", err)
	assert.Equal(t, tfw.StatusErrUnsupported, st)
}

// The two nonce widths are not interchangeable: the CLI refuses a swapped one
// before a frame is sent, so the device is never reached (a nil device here
// would panic if it were).
func TestE2EAttestRejectsAWrongLengthNonceBeforeTheWire(t *testing.T) {
	_, err := cliHex(t, nil, "attest-cert", "certificate", attestNonce(1))
	require.Error(t, err, "a 16-byte nonce for attest-cert")
	assert.True(t, strings.Contains(err.Error(), "want exactly"), "attest-cert: %v", err)
	_, err = cliHex(t, nil, "attest", "evidence", aliasNonce(1))
	require.Error(t, err, "a 32-byte nonce for attest")
	assert.True(t, strings.Contains(err.Error(), "want exactly"), "attest: %v", err)
}
