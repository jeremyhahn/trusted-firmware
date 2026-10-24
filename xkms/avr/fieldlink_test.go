//go:build tfwsim

// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"context"
	"crypto/ecdsa"
	"crypto/x509"
	"io"
	"log/slog"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// These tests hold the adapter to the field link of onboarding (row
// device-onboarding, docs/device-onboarding/architecture.md steps 8-15): the
// REAL firmware, compiled for the host, dials a listener; the backend is built
// by the registry's factory over the connection Accept returned, as go-quicbaas
// builds it; and only the calls the Ethernet allow-list admits
// (src/proto/link_eth.c, tfw_link_admits) are expected to reach the device.
// The same firmware's serial link stays open beside it, so each refusal on the
// field link is shown to be the link's and not the image's.

// fieldLink starts the element-tier firmware with both links and returns a
// backend built by NewKeyProvider over each.
func fieldLink(t *testing.T) (serial, eth *Backend, ethDevice *tfw.TCPDevice) {
	t.Helper()
	serialDevice, ethDevice := tfwsim.StartElementEthernet(t)
	return fieldBackend(t, serialDevice), fieldBackend(t, ethDevice.Device), ethDevice
}

func fieldBackend(t *testing.T, device *tfw.Device) *Backend {
	t.Helper()
	cfg := DefaultConfig()
	cfg.Logger = slog.New(slog.NewTextHandler(io.Discard, nil))
	provider, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: device,
		ConfigKeyConfig: cfg,
	})
	require.NoError(t, err)
	b, ok := provider.(*Backend)
	require.True(t, ok, "the factory returned a %T", provider)
	t.Cleanup(func() { assert.NoError(t, b.Close()) })
	return b
}

func fieldAttest(t *testing.T, b *Backend, keyAuth string) *Attestation {
	t.Helper()
	result, err := b.AttestKey(context.Background(),
		&types.KeyAttributes{CN: DefaultKeyCN}, deviceattest01.DICENonce(keyAuth))
	require.NoError(t, err)
	att, ok := result.(*Attestation)
	require.True(t, ok, "AttestKey returned a %T", result)
	return att
}

// Step 9: the factory reads CAPS over the accepted connection, and every
// answer derived from it is the one the serial link gives for the same image.
func TestFieldLinkBackendReadsCapsOverEthernet(t *testing.T) {
	serial, eth, _ := fieldLink(t)

	assert.Equal(t, serial.Caps(), eth.Caps())
	assert.Equal(t, types.BackendType(BackendType), eth.Type())
	assert.Equal(t, serial.Algorithms(), eth.Algorithms())
	assert.True(t, eth.Algorithms().Can(AlgorithmECDSAP256SHA256), "the element signs P-256")

	level, err := eth.AttestLevel()
	require.NoError(t, err)
	assert.Equal(t, types.AttestLevelHardware, level, "an element-tier device attests in hardware")
	assert.True(t, eth.Caps().Features&tfw.FeatureAttestCSR != 0,
		"the element tier carries ATTEST_CSR, so the part can enrol")
}

// Steps 11 and 13 over the field link: the evidence names the device by the
// UEID its platform block holds, certifies the provisioned DeviceID key, and
// the possession signer signs go-xkms's request for the same nonce.
func TestFieldLinkAttestsAndProvesPossessionOverEthernet(t *testing.T) {
	_, eth, _ := fieldLink(t)
	nonce := deviceattest01.DICENonce("field-link-enrol")
	att := fieldAttest(t, eth, "field-link-enrol")

	assert.Equal(t, nonce, att.Nonce)
	assert.Equal(t, append([]byte{0x01}, tfwsim.TestSerial[:]...), att.Ueid,
		"who the device is comes from its evidence: 0x01 || the baked serial")
	identity, err := deviceattest01.DeviceIdentityFromCertificate(deviceattest01.FormatDICE, att.Certificate)
	require.NoError(t, err)
	assert.Len(t, identity, 34, "the permanent-identifier is 34 lowercase hex digits")

	leafKey, ok := att.Certificate.PublicKey.(*ecdsa.PublicKey)
	require.True(t, ok, "the alias certificate certifies a %T", att.Certificate.PublicKey)
	assert.True(t, leafKey.Equal(tfwsim.TestDeviceIDPublicKey()),
		"on the field link the device's key is read from its evidence")
	require.Len(t, att.Chain, 1, "no intermediates configured: the leaf alone")
	assert.Equal(t, att.CertificateDER, att.Chain[0])

	signer, err := eth.PossessionSigner(att)
	require.NoError(t, err)
	der, err := deviceattest01.SignPossessionCSR(signer, att.Claims, nonce)
	require.NoError(t, err)
	csr, err := x509.ParseCertificateRequest(der)
	require.NoError(t, err)
	require.NoError(t, csr.CheckSignature(), "the device's signature verifies over its own request")
	assert.True(t, csr.PublicKey.(*ecdsa.PublicKey).Equal(leafKey),
		"the request is for the attested key")
}

// The bench calls are not on the Ethernet allow-list: over the field link
// Health (SYS_INFO), Identity (PROV_INFO) and GetKey (PROV_PUBKEY) answer
// ErrCommandUnavailable. Health is the link's refusal, not the image's: the
// same firmware answers it on serial. The other two are PROV_* commands, which
// a production image carries no handler for on any link, so on a fielded
// device the key is read from the evidence (AttestKey), never from GetKey. A
// refusal leaves the connection in step: the next attestation over it still
// succeeds.
func TestFieldLinkRefusesBenchCallsAndStaysInStep(t *testing.T) {
	serial, eth, _ := fieldLink(t)
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	_, err := serial.Health(ctx)
	require.NoError(t, err, "serial: SYS_INFO")

	for _, tc := range []struct {
		name string
		call func() error
	}{
		{"Identity", func() error { _, err := eth.Identity(ctx); return err }},
		{"Health", func() error { _, err := eth.Health(ctx); return err }},
		{"GetKey", func() error { _, err := eth.GetKey(ctx, attrs); return err }},
	} {
		err := tc.call()
		assert.ErrorIs(t, err, ErrCommandUnavailable, "ethernet: %s", tc.name)
		assert.ErrorIs(t, err, tfw.StatusErrUnknownCmd, "ethernet: %s carries the device's status", tc.name)
	}

	att := fieldAttest(t, eth, "after-refusals")
	assert.Equal(t, deviceattest01.DICENonce("after-refusals"), att.Nonce)
}

// Step 15: re-attestation over the same connection, after enrolment's
// possession signature. Each answer is bound to its own nonce and names the
// same device and key; freshness is the verifier's nonce alone.
//
// The device keeps the measurement of its latest mint only
// (tfw_alias_measurement_for, src/caps/alias.c), so once it has attested to a
// new nonce the earlier nonce's possession request is refused with BAD_ARGC:
// an enrolment's ATTEST_CERT and its finalize's ATTEST_CSR must not have
// another attestation between them on the device.
func TestFieldLinkReattestsOnTheSameConnection(t *testing.T) {
	_, eth, _ := fieldLink(t)
	first := fieldAttest(t, eth, "enrol")
	signer, err := eth.PossessionSigner(first)
	require.NoError(t, err)
	_, err = deviceattest01.SignPossessionCSR(signer, first.Claims, first.Nonce)
	require.NoError(t, err)

	again := fieldAttest(t, eth, "re-attest")
	assert.Equal(t, deviceattest01.DICENonce("re-attest"), again.Nonce)
	assert.NotEqual(t, first.CertificateDER, again.CertificateDER, "a new nonce is a new certificate")
	assert.Equal(t, first.Ueid, again.Ueid)
	assert.True(t, first.Certificate.PublicKey.(*ecdsa.PublicKey).Equal(again.Certificate.PublicKey))

	_, err = deviceattest01.SignPossessionCSR(signer, first.Claims, first.Nonce)
	assert.ErrorIs(t, err, ErrInvalidArgument, "the earlier nonce has no certificate behind it now")
	assert.ErrorIs(t, err, tfw.StatusErrBadArgc)

	latest, err := eth.PossessionSigner(again)
	require.NoError(t, err)
	_, err = deviceattest01.SignPossessionCSR(latest, again.Claims, again.Nonce)
	require.NoError(t, err, "the latest attestation's request is signed")
}

// The backend never owns the link: closing it leaves the accepted connection
// to the endpoint, which can build the next backend over it.
func TestFieldLinkCloseLeavesTheConnection(t *testing.T) {
	_, eth, ethDevice := fieldLink(t)
	require.NoError(t, eth.Close())
	_, err := eth.AttestKey(context.Background(), &types.KeyAttributes{CN: DefaultKeyCN},
		deviceattest01.DICENonce("closed"))
	assert.ErrorIs(t, err, ErrBackendClosed)

	caps, err := ethDevice.Caps(context.Background())
	require.NoError(t, err, "the connection outlives the backend")
	assert.Equal(t, eth.Caps(), *caps)

	next := fieldBackend(t, ethDevice.Device)
	att := fieldAttest(t, next, "next-backend")
	assert.Equal(t, deviceattest01.DICENonce("next-backend"), att.Nonce)
}
