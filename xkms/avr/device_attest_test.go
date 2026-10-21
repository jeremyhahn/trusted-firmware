// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/asn1"
	"encoding/hex"
	"fmt"
	"math/big"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/acme/entities"
	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
	"github.com/jeremyhahn/go-xkms/pkg/types"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// These tests hold the adapter's half of a device-attest-01 enrolment to
// go-xkms's server half, on the frames the real firmware answered
// (device_attest_vectors_test.go): the evidence the adapter reassembles is
// what go-xkms's verifier accepts, the identity it reads is the device's, and
// the possession request the adapter has the device sign is the one go-xkms's
// finalize checks pass. Each refusal names the check that made it, so a
// refusal for the wrong reason fails. The whole ACME exchange over a board is
// xkms/e2e TestHardwareEnrollment's.

func unhex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	require.NoError(t, err)
	return b
}

// recordedLink answers what the element simulator answered: its CAPS, the
// alias certificate pages for one nonce and its possession signature.
func recordedLink(t *testing.T, pages []string, csrHex string) *fakeLink {
	t.Helper()
	link := newFakeLink().
		answer(tfw.CmdCaps, unhex(t, recCapsHex)).
		answer(tfw.CmdAttestCSR, unhex(t, csrHex))
	for _, page := range pages {
		link.pagedReplies[tfw.CmdAttestCert] = append(link.pagedReplies[tfw.CmdAttestCert], unhex(t, page))
	}
	return link
}

// recordedAttestation attests over keyAuth's nonce through the adapter, on a
// link answering the recorded frames.
func recordedAttestation(t *testing.T, pages []string, csrHex, keyAuth string) (*Backend, *Attestation) {
	t.Helper()
	b := newTestBackend(t, recordedLink(t, pages, csrHex))
	result, err := b.AttestKey(context.Background(), &types.KeyAttributes{CN: DefaultKeyCN}, deviceattest01.DICENonce(keyAuth))
	require.NoError(t, err)
	return b, result.(*Attestation)
}

// deviceRegistry is the provisioning record store a server resolves a ueid in.
type deviceRegistry map[string]*deviceattest01.ProvisionedDevice

func (r deviceRegistry) ResolveDevice(_ context.Context, ueid string) (*deviceattest01.ProvisionedDevice, error) {
	record, ok := r[ueid]
	if !ok {
		return nil, fmt.Errorf("%w: %s", deviceattest01.ErrDeviceNotProvisioned, ueid)
	}
	return record, nil
}

func provisioned(spki []byte) deviceRegistry {
	return deviceRegistry{recUeidHex: {DeviceIDKey: spki}}
}

// recordedVerifier is the verifier a server enrolling this profile runs: DICE,
// a device-signed leaf, anchored in the provisioning record.
func recordedVerifier(t *testing.T, devices deviceattest01.DeviceResolver) *deviceattest01.TrustAnchorVerifier {
	t.Helper()
	verifier, err := deviceattest01.NewTrustAnchorVerifier(deviceattest01.TrustAnchors{
		Devices: devices,
		DICE:    deviceattest01.DICEPolicy{RequireDeviceSignedLeaf: true},
	})
	require.NoError(t, err)
	return verifier
}

// attestationRequest is the challenge POST body carrying att, as a client
// answering the challenge sends it.
func attestationRequest(t *testing.T, att *Attestation) []byte {
	t.Helper()
	payload, err := deviceattest01.EncodeAttestationRequest(&deviceattest01.AttestationObject{
		Format:       deviceattest01.FormatDICE.String(),
		AttStatement: deviceattest01.AttestationStatement{X5c: []interface{}{att.CertificateDER}},
	})
	require.NoError(t, err)
	return payload
}

func verifyRecorded(t *testing.T, verifier *deviceattest01.TrustAnchorVerifier, att *Attestation) *deviceattest01.VerifiedAttestation {
	t.Helper()
	verified, err := deviceattest01.VerifyAttestation(context.Background(), verifier, attestationRequest(t, att), recKeyAuth)
	require.NoError(t, err)
	return verified
}

func parseCSR(t *testing.T, der []byte) *x509.CertificateRequest {
	t.Helper()
	csr, err := x509.ParseCertificateRequest(der)
	require.NoError(t, err)
	return csr
}

func ueidIdentifiers(value string) []entities.ACMEIdentifier {
	return []entities.ACMEIdentifier{{Type: deviceattest01.PossessionIdentifierType, Value: value}}
}

func recordedDeviceID(t *testing.T) *ecdsa.PublicKey {
	t.Helper()
	pub, err := x509.ParsePKIXPublicKey(unhex(t, recDeviceIDSPKIHex))
	require.NoError(t, err)
	return pub.(*ecdsa.PublicKey)
}

func TestDeviceAttestAcceptsTheRecordedEnrolment(t *testing.T) {
	b, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)

	identity, err := deviceattest01.DeviceIdentityFromCertificate(deviceattest01.FormatDICE, att.Certificate)
	require.NoError(t, err)
	assert.Equal(t, recUeidHex, identity, "the identity is the hex of the ueid, type byte included")
	assert.Equal(t, unhex(t, recUeidHex), att.Ueid)
	assert.True(t, att.Certificate.PublicKey.(*ecdsa.PublicKey).Equal(recordedDeviceID(t)),
		"the attested key is the provisioned DeviceID key")

	verifier := recordedVerifier(t, provisioned(unhex(t, recDeviceIDSPKIHex)))
	verified := verifyRecorded(t, verifier, att)
	served, err := verified.DeviceIdentity()
	require.NoError(t, err)
	assert.Equal(t, identity, served, "client and server read one identity from the evidence")
	require.NoError(t, verified.ConfirmRegistration(context.Background(), verifier))

	signer, err := b.PossessionSigner(att)
	require.NoError(t, err)
	der, err := deviceattest01.SignPossessionCSR(signer, att.Claims, att.Nonce)
	require.NoError(t, err, "the device signed the request go-xkms builds, byte for byte")
	csr := parseCSR(t, der)
	require.NoError(t, verified.VerifyPossessionCSR(csr, ueidIdentifiers(identity)))
	require.NoError(t, verified.BindCSRKey(csr))
	assert.True(t, csr.PublicKey.(*ecdsa.PublicKey).Equal(recordedDeviceID(t)),
		"the request is for the key the device attested")

	request, err := deviceattest01.ParsePossessionCSR(csr)
	require.NoError(t, err)
	assert.Equal(t, identity, request.Identifier().Value, "the request names the device by its ueid")
}

func TestDeviceAttestRefusesARequestNamingAnotherUeid(t *testing.T) {
	b, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)
	verified := verifyRecorded(t, recordedVerifier(t, provisioned(unhex(t, recDeviceIDSPKIHex))), att)
	signer, err := b.PossessionSigner(att)
	require.NoError(t, err)
	der, err := deviceattest01.SignPossessionCSR(signer, att.Claims, att.Nonce)
	require.NoError(t, err)

	another := hex.EncodeToString(append([]byte{0x01}, []byte("another-device-1")...))
	err = verified.VerifyPossessionCSR(parseCSR(t, der), ueidIdentifiers(another))

	assert.ErrorIs(t, err, deviceattest01.ErrPossessionIdentifierMismatch)
}

func TestDeviceAttestRefusesEvidenceForAnotherChallenge(t *testing.T) {
	_, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)
	verifier := recordedVerifier(t, provisioned(unhex(t, recDeviceIDSPKIHex)))

	_, err := deviceattest01.VerifyAttestation(context.Background(), verifier, attestationRequest(t, att), recAnotherKeyAuth)

	assert.ErrorIs(t, err, dice.ErrNonceMismatch,
		"the verifier must refuse the evidence for its nonce, not for another reason")
}

func TestDeviceAttestRefusesARequestForAnotherKey(t *testing.T) {
	_, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)
	verified := verifyRecorded(t, recordedVerifier(t, provisioned(unhex(t, recDeviceIDSPKIHex))), att)
	other, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	der, err := deviceattest01.SignPossessionCSR(other, att.Claims, att.Nonce)
	require.NoError(t, err)
	csr := parseCSR(t, der)

	assert.ErrorIs(t, verified.VerifyPossessionCSR(csr, ueidIdentifiers(recUeidHex)), deviceattest01.ErrPossessionKeyMismatch)
	assert.Error(t, verified.BindCSRKey(csr))
}

// The device really signed a request for another challenge's nonce: the
// signature verifies, the evidence was right, and the request is still refused
// for its nonce.
func TestDeviceAttestRefusesTheDevicesRequestForAnotherNonce(t *testing.T) {
	_, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)
	verified := verifyRecorded(t, recordedVerifier(t, provisioned(unhex(t, recDeviceIDSPKIHex))), att)

	cri, err := deviceattest01.BuildPossessionCRI(recordedDeviceID(t), att.Claims, deviceattest01.DICENonce(recAnotherKeyAuth))
	require.NoError(t, err)
	raw := unhex(t, recAnotherAttestCSRHex)
	sig, err := asn1.Marshal(struct{ R, S *big.Int }{
		new(big.Int).SetBytes(raw[:tfw.CSRSigLen/2]),
		new(big.Int).SetBytes(raw[tfw.CSRSigLen/2:]),
	})
	require.NoError(t, err)
	der, err := deviceattest01.AssemblePossessionCSR(cri, sig)
	require.NoError(t, err)
	csr := parseCSR(t, der)
	require.NoError(t, csr.CheckSignature(), "the recorded signature is the device's over this request")

	assert.ErrorIs(t, verified.VerifyPossessionCSR(csr, ueidIdentifiers(recUeidHex)), deviceattest01.ErrPossessionNonceMismatch)
}

// The adapter's own check: a signer made for one challenge's attestation
// refuses a request built for another's, before any server sees it.
func TestPossessionSignerRefusesTheRecordedRequestForAnotherNonce(t *testing.T) {
	b, att := recordedAttestation(t, recAnotherAttestCertPages, recAnotherAttestCSRHex, recAnotherKeyAuth)
	signer, err := b.PossessionSigner(att)
	require.NoError(t, err)

	_, err = deviceattest01.SignPossessionCSR(signer, att.Claims, att.Nonce)
	require.NoError(t, err, "the recording answers its own challenge")

	_, err = deviceattest01.SignPossessionCSR(signer, att.Claims, deviceattest01.DICENonce(recKeyAuth))
	assert.ErrorIs(t, err, ErrPossessionMismatch)
}

func TestDeviceAttestRefusesAnotherProvisionedKey(t *testing.T) {
	_, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)
	stranger, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	spki, err := x509.MarshalPKIXPublicKey(&stranger.PublicKey)
	require.NoError(t, err)

	_, err = deviceattest01.VerifyAttestation(context.Background(), recordedVerifier(t, provisioned(spki)),
		attestationRequest(t, att), recKeyAuth)

	assert.ErrorIs(t, err, deviceattest01.ErrDeviceNotAuthorized)
	assert.ErrorIs(t, err, deviceattest01.ErrAttestedKeyMismatch)
	assert.NotErrorIs(t, err, dice.ErrNonceMismatch, "the nonce was right: the refusal must be about the record")
}

func TestDeviceAttestRefusesAnUnprovisionedDevice(t *testing.T) {
	_, att := recordedAttestation(t, recAttestCertPages, recAttestCSRHex, recKeyAuth)

	_, err := deviceattest01.VerifyAttestation(context.Background(), recordedVerifier(t, deviceRegistry{}),
		attestationRequest(t, att), recKeyAuth)

	assert.ErrorIs(t, err, deviceattest01.ErrDeviceNotAuthorized)
	assert.ErrorIs(t, err, deviceattest01.ErrDeviceNotProvisioned)
}
