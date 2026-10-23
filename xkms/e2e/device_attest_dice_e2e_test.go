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
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"fmt"
	"io"
	"log/slog"
	"math/big"
	"sync/atomic"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/require"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/acme/client"
	"github.com/jeremyhahn/go-xkms/pkg/ca"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// The device-attest-01 enrolment TestHardwareEnrollment (hw_enroll_test.go)
// runs, through go-xkms's ACME client and the real ACME server handlers with
// device-attest-01 offered, against a board or the simulator:
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
// What the adapter contributes to each step, and each refusal the server
// makes, is unit-tested in xkms/avr (device_attest_test.go) on frames recorded
// from the simulator; this file is only the harness the enrolment runs in.

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

	backend  *avr.Backend
	identity string
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
		backend: backend,
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
	att, err := e.attest(deviceattest01.DICENonce(keyAuthorization))
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
	signer, err := e.backend.PossessionSigner(att)
	if err != nil {
		return nil, err
	}
	return &client.AttestedKey{Statement: statement, Signer: signer, Format: deviceattest01.FormatDICE}, nil
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
	// verified is what the last accepted "dice" evidence established, kept
	// so a test reads what go-xkms recorded about the device (its RootType).
	verified atomic.Pointer[deviceattest01.VerifiedAttestation]
}

func (r *verdictRecorder) VerifyAttestationObject(obj *deviceattest01.AttestationObject, expectedChallenge []byte) error {
	return r.record(r.TrustAnchorVerifier.VerifyAttestationObject(obj, expectedChallenge))
}

func (r *verdictRecorder) VerifyAKCertificateChain(certs [][]byte) error {
	return r.record(r.TrustAnchorVerifier.VerifyAKCertificateChain(certs))
}

func (r *verdictRecorder) VerifyRegisteredDevice(ctx context.Context, leaf *x509.Certificate, keyAuthorization string) (*deviceattest01.VerifiedAttestation, error) {
	verified, err := r.TrustAnchorVerifier.VerifyRegisteredDevice(ctx, leaf, keyAuthorization)
	if err == nil {
		r.verified.Store(verified)
	}
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

// Verified returns what the last accepted "dice" evidence established, or nil.
func (r *verdictRecorder) Verified() *deviceattest01.VerifiedAttestation {
	return r.verified.Load()
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
