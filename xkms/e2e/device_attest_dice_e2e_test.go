//go:build codec_json

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

package e2e

import (
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/hex"
	"fmt"
	"io"
	"log/slog"
	"math/big"
	"sync/atomic"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/acme/client"
	"github.com/jeremyhahn/go-xkms/pkg/attestation/dice"
	"github.com/jeremyhahn/go-xkms/pkg/ca"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// These tests enroll the REAL trusted-firmware firmware, compiled for the host
// and started by tfwsim.StartElement, through go-xkms's ACME client and the real
// ACME server handlers with device-attest-01 offered:
//
//   - the device mints a DICE alias certificate over the nonce derived from the
//     challenge's key authorization (ATTEST_CERT, signed with real P-256);
//   - the client POSTs it as the attestation object and the server verifies it
//     with the production deviceattest01.TrustAnchorVerifier, anchored in the
//     device's provisioning record;
//   - the order's identifier is the device's ueid, read from the evidence;
//   - finalization sends the possession request, signed by the device itself
//     over the same nonce (ATTEST_CSR, through avr.Backend.PossessionSigner).
//
// The simulator binaries are built by `make tfwsim`. A missing binary FAILS
// these tests (tfwsim reports the command); they never skip.

// provisioningRecord is the record a provisioning step writes for a device:
// its ueid and the DeviceID public key read from it at the factory (the
// raw-key path of deviceattest01.ProvisionedDevice). The alias certificate
// must carry that key and verify under it; only the public key crosses over.
func provisioningRecord(t *testing.T, ueid string, deviceID *ecdsa.PublicKey) deviceRegistry {
	t.Helper()
	spki, err := x509.MarshalPKIXPublicKey(deviceID)
	require.NoError(t, err)
	return deviceRegistry{ueid: {DeviceIDKey: spki}}
}

// deviceRegistry is the device registry the server anchors "dice" evidence
// in, keyed by the hex of the ueid. go-trusted-platform owns the real one;
// this holds only what a provisioning step would have written.
type deviceRegistry map[string]*deviceattest01.ProvisionedDevice

// ResolveDevice implements deviceattest01.DeviceResolver.
func (r deviceRegistry) ResolveDevice(_ context.Context, ueid string) (*deviceattest01.ProvisionedDevice, error) {
	record, ok := r[ueid]
	if !ok {
		return nil, fmt.Errorf("%w: %s", deviceattest01.ErrDeviceNotProvisioned, ueid)
	}
	return record, nil
}

// diceElement is the ACME client's view of the simulated device. It implements
// the two TPMModule methods DeviceAttest calls for a device-attest-01 order,
// DeviceIdentity and CreateAttestedKey; the embedded interface is nil, so a
// call to any other method fails loudly rather than answering.
//
// The device is the real firmware, reached through the adapter's backend: the
// challenge is answered with AttestKey's alias certificate and finalize is
// signed by the device's ATTEST_CSR through PossessionSigner. No private key
// is held on the host.
type diceElement struct {
	client.TPMModule

	device   *tfw.Device
	backend  *avr.Backend
	identity string

	// nonceFor derives the nonce the device attests over from the key
	// authorization. The draft's is deviceattest01.DICENonce.
	nonceFor func(keyAuthorization string) []byte

	// signerFor returns the finalize signer for the attestation the device
	// made for the challenge. The adapter's is Backend.PossessionSigner.
	signerFor func(att *avr.Attestation) (crypto.Signer, error)
}

func newDiceElement(t *testing.T) *diceElement {
	t.Helper()
	return newDiceElementOn(t, tfwsim.StartElement(t))
}

// newDiceElementOn is the element on a device the caller opened: the
// simulator, or a board on the bench (hw_enroll_test.go). Everything after the
// link is the same flow whichever it is.
func newDiceElementOn(t *testing.T, device *tfw.Device) *diceElement {
	t.Helper()
	cfg := avr.DefaultConfig()
	cfg.Logger = slog.New(slog.NewTextHandler(io.Discard, nil))
	backend, err := avr.NewBackend(context.Background(), cfg, device)
	require.NoError(t, err)

	element := &diceElement{
		device:    device,
		backend:   backend,
		nonceFor:  deviceattest01.DICENonce,
		signerFor: backend.PossessionSigner,
	}

	// The device's identity is read from its own evidence, by the rule the
	// server reads it with, so client and server cannot disagree.
	probe, err := element.attest(deviceattest01.DICENonce("identity-probe"))
	require.NoError(t, err)
	element.identity, err = deviceattest01.DeviceIdentityFromCertificate(deviceattest01.FormatDICE, probe.Certificate)
	require.NoError(t, err)
	return element
}

func (e *diceElement) attest(nonce []byte) (*avr.Attestation, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	result, err := e.backend.AttestKey(ctx, &types.KeyAttributes{CN: avr.DefaultKeyCN}, nonce)
	if err != nil {
		return nil, err
	}
	return result.(*avr.Attestation), nil
}

// DeviceIdentity implements TPMModule.
func (e *diceElement) DeviceIdentity() (string, error) { return e.identity, nil }

// CreateAttestedKey implements TPMModule: the device mints its alias
// certificate over the challenge's nonce, the client wraps it as a "dice"
// attestation object, and the possession signer for that same nonce signs
// the finalize request.
func (e *diceElement) CreateAttestedKey(_, keyAuthorization string) (*client.AttestedKey, error) {
	att, err := e.attest(e.nonceFor(keyAuthorization))
	if err != nil {
		return nil, err
	}
	statement, err := deviceattest01.EncodeAttestationRequest(&deviceattest01.AttestationObject{
		Format:       deviceattest01.FormatDICE.String(),
		AttStatement: deviceattest01.AttestationStatement{X5c: []interface{}{att.CertificateDER}},
	})
	if err != nil {
		return nil, err
	}
	signer, err := e.signerFor(att)
	if err != nil {
		return nil, err
	}
	return &client.AttestedKey{Statement: statement, Signer: signer, Format: deviceattest01.FormatDICE}, nil
}

// uncheckedDeviceSigner hands the server whatever ATTEST_CSR returns for its
// nonce, without the check PossessionSigner makes, so a test can show the
// server refusing what the adapter would have refused first.
type uncheckedDeviceSigner struct {
	device *tfw.Device
	pub    crypto.PublicKey
	nonce  [tfw.AliasNonceLen]byte
}

func (s *uncheckedDeviceSigner) Public() crypto.PublicKey { return s.pub }

func (s *uncheckedDeviceSigner) Sign(io.Reader, []byte, crypto.SignerOpts) ([]byte, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	raw, err := s.device.AttestCSR(ctx, s.nonce)
	if err != nil {
		return nil, err
	}
	half := len(raw) / 2
	return asn1.Marshal(struct{ R, S *big.Int }{
		new(big.Int).SetBytes(raw[:half]),
		new(big.Int).SetBytes(raw[half:]),
	})
}

// verdictRecorder wraps the production verifier and remembers why it last
// refused. The server hides that reason from the client by design -- every
// failed attestation is the same incorrectResponse -- so a test that must tell
// "the nonce was wrong" from "the root was untrusted" reads it here, where the
// verdict is made, instead of weakening what the client is told.
//
// It embeds the concrete verifier, not the AttestationVerifier interface: a
// "dice" attestation is anchored through RegisteredDeviceVerifier, and a
// wrapper that hid it would be refused as having no device registry.
type verdictRecorder struct {
	*deviceattest01.TrustAnchorVerifier
	refusal atomic.Pointer[error]
}

func (r *verdictRecorder) VerifyAttestationObject(obj *deviceattest01.AttestationObject, expectedChallenge []byte) error {
	return r.record(r.TrustAnchorVerifier.VerifyAttestationObject(obj, expectedChallenge))
}

func (r *verdictRecorder) VerifyAKCertificateChain(certs [][]byte) error {
	return r.record(r.TrustAnchorVerifier.VerifyAKCertificateChain(certs))
}

func (r *verdictRecorder) VerifyRegisteredDevice(ctx context.Context, leaf *x509.Certificate, keyAuthorization string) (*deviceattest01.VerifiedAttestation, error) {
	verified, err := r.TrustAnchorVerifier.VerifyRegisteredDevice(ctx, leaf, keyAuthorization)
	return verified, r.record(err)
}

func (r *verdictRecorder) ConfirmRegisteredDevice(ctx context.Context, ueid string, attestedSPKI []byte) error {
	return r.record(r.TrustAnchorVerifier.ConfirmRegisteredDevice(ctx, ueid, attestedSPKI))
}

func (r *verdictRecorder) record(err error) error {
	if err != nil {
		r.refusal.Store(&err)
	}
	return err
}

// Refusal returns the last error the verifier refused with, or nil.
func (r *verdictRecorder) Refusal() error {
	if refusal := r.refusal.Load(); refusal != nil {
		return *refusal
	}
	return nil
}

// diceEnrollment is a device-attest-01 ACME server anchored the way a
// deployment anchors it, and a client for it.
type diceEnrollment struct {
	server   *deviceAttestServer
	client   *client.Client
	verifier *verdictRecorder
	issuer   *x509.Certificate
}

// newDiceEnrollment serves device-attest-01 with "dice" evidence anchored in
// devices. RequireDeviceSignedLeaf is the AVR profile the xkmsd configuration
// sets: the alias certificate's issuer is the element's own DeviceID key.
func newDiceEnrollment(t *testing.T, devices deviceattest01.DeviceResolver) *diceEnrollment {
	t.Helper()
	verifier, err := deviceattest01.NewTrustAnchorVerifier(deviceattest01.TrustAnchors{
		Devices: devices,
		DICE:    deviceattest01.DICEPolicy{RequireDeviceSignedLeaf: true},
	})
	require.NoError(t, err)

	caKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	template := &x509.Certificate{
		SerialNumber:          big.NewInt(1),
		Subject:               pkix.Name{CommonName: "Issuing CA"},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().Add(24 * time.Hour),
		KeyUsage:              x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &caKey.PublicKey, caKey)
	require.NoError(t, err)
	caCert, err := x509.ParseCertificate(der)
	require.NoError(t, err)

	recorder := &verdictRecorder{TrustAnchorVerifier: verifier}
	server := startDeviceAttestServer(t, caCert, caKey, recorder)
	return &diceEnrollment{server: server, client: newDeviceAttestRoundTripClient(t, server), verifier: recorder, issuer: caCert}
}

func (e *diceEnrollment) enroll(element client.TPMModule) (*x509.Certificate, error) {
	return deviceAttest(e.client, element)
}

// deviceAttest is the one device-attest-01 exchange, against whichever ACME
// server the client was made for: the in-process one, or a control plane on
// the bench.
func deviceAttest(acmeClient *client.Client, element client.TPMModule) (*x509.Certificate, error) {
	return acmeClient.DeviceAttest(context.Background(), &client.EnrollmentConfig{
		Challenge:  string(client.ChallengeTypeDeviceAttest01),
		TPMModule:  element,
		CommonName: "sensor-1.example.com",
		Timeout:    10 * time.Second,
	})
}

// permanentIdentifier returns the RFC 4043 permanentIdentifier a certificate
// is bound to.
func permanentIdentifier(t *testing.T, cert *x509.Certificate) string {
	t.Helper()
	names, err := ca.ParseOtherNames(cert.Extensions)
	require.NoError(t, err)
	for _, name := range names {
		if name.TypeID.Equal(ca.OIDPermanentIdentifier) {
			parsed, err := ca.ParsePermanentIdentifierExtension(name.Value)
			require.NoError(t, err)
			return parsed.IdentifierValue
		}
	}
	t.Fatal("the certificate carries no permanentIdentifier")
	return ""
}

// A real trusted-firmware device enrolls: the order is for its ueid, its DICE
// evidence answers the challenge's nonce, and the certificate that is issued
// is for the DeviceID key it attested and bound to the ueid.
func TestDeviceAttestDICEEnrollsTheRealFirmware(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))

	assert.Equal(t, hex.EncodeToString(append([]byte{0x01}, tfwsim.TestSerial[:]...)), element.identity,
		"the identity is the hex of the ueid, type byte included")

	cert, err := enrollment.enroll(element)
	require.NoError(t, err)
	require.NoError(t, enrollment.verifier.Refusal())

	assert.True(t, cert.PublicKey.(*ecdsa.PublicKey).Equal(tfwsim.TestDeviceIDPublicKey()),
		"the certificate must be for the key the device attested")
	assert.Equal(t, element.identity, permanentIdentifier(t, cert),
		"the certificate must be bound to the device's ueid")
	select {
	case issued := <-enrollment.server.issued:
		assert.Equal(t, issued.Raw, cert.Raw)
	default:
		t.Fatal("the server issued no certificate")
	}
}

// An order for a ueid other than the one the evidence carries is refused: the
// evidence is genuine, but it is not that device's.
func TestDeviceAttestDICERefusesAnotherUeid(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))
	element.identity = hex.EncodeToString(append([]byte{0x01}, []byte("another-device-1")...))

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:unauthorized")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
}

// Evidence minted over a different challenge's nonce is a replay and is
// refused, whatever else about it is genuine.
func TestDeviceAttestDICERefusesTheWrongNonce(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))
	element.nonceFor = func(string) []byte { return deviceattest01.DICENonce("another-token.another-thumbprint") }

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:incorrectResponse")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
	assert.ErrorIs(t, enrollment.verifier.Refusal(), dice.ErrNonceMismatch,
		"the verifier must have refused the evidence for its nonce, not for another reason")
}

// A possession request signed by any key but the attested one is refused at
// finalization, even though the device's evidence verified.
func TestDeviceAttestDICERefusesACSRForAnotherKey(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))
	other, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	element.signerFor = func(*avr.Attestation) (crypto.Signer, error) { return other, nil }

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:badCSR")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
}

// The device's possession signature for another nonce is refused by the
// server: the request it signed is not the one for this challenge.
func TestDeviceAttestDICERefusesACSRForAnotherNonce(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))
	element.signerFor = func(att *avr.Attestation) (crypto.Signer, error) {
		signer := &uncheckedDeviceSigner{device: element.device, pub: att.Certificate.PublicKey}
		copy(signer.nonce[:], deviceattest01.DICENonce("another-token.another-thumbprint"))
		return signer, nil
	}

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:badCSR")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
	assert.NoError(t, enrollment.verifier.Refusal(), "the evidence was right: the refusal must be about the CSR")
}

// The adapter refuses the same request before it is sent: PossessionSigner
// for an attestation made over another nonce returns no signature for this
// challenge's request, and nothing is finalized.
func TestDeviceAttestDICEPossessionSignerRefusesAnotherNoncesRequest(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, tfwsim.TestDeviceIDPublicKey()))
	element.signerFor = func(*avr.Attestation) (crypto.Signer, error) {
		other, err := element.attest(deviceattest01.DICENonce("another-token.another-thumbprint"))
		if err != nil {
			return nil, err
		}
		return element.backend.PossessionSigner(other)
	}

	cert, err := enrollment.enroll(element)

	// The ACME client reports the cause as text, not as a chain.
	require.ErrorContains(t, err, avr.ErrPossessionMismatch.Error())
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
}

// Evidence whose key is not the one the device's provisioning record holds is
// refused: the alias certificate is self-consistent whoever made it.
func TestDeviceAttestDICERefusesAnotherProvisionedKey(t *testing.T) {
	element := newDiceElement(t)
	stranger, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	enrollment := newDiceEnrollment(t, provisioningRecord(t, element.identity, &stranger.PublicKey))

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:unauthorized")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
	refusal := enrollment.verifier.Refusal()
	assert.ErrorIs(t, refusal, deviceattest01.ErrDeviceNotAuthorized)
	assert.ErrorIs(t, refusal, deviceattest01.ErrAttestedKeyMismatch)
	assert.NotErrorIs(t, refusal, dice.ErrNonceMismatch,
		"the nonce was right: the refusal must be about the record")
}

// A device nobody provisioned is refused, however genuine its evidence: there
// is no record to anchor it in.
func TestDeviceAttestDICERefusesAnUnprovisionedDevice(t *testing.T) {
	element := newDiceElement(t)
	enrollment := newDiceEnrollment(t, deviceRegistry{})

	cert, err := enrollment.enroll(element)

	require.ErrorContains(t, err, "urn:ietf:params:acme:error:unauthorized")
	assert.Nil(t, cert)
	assert.Empty(t, enrollment.server.issued)
	refusal := enrollment.verifier.Refusal()
	assert.ErrorIs(t, refusal, deviceattest01.ErrDeviceNotAuthorized)
	assert.ErrorIs(t, refusal, deviceattest01.ErrDeviceNotProvisioned)
}
