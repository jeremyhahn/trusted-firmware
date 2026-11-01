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

package xkms_test

import (
	"context"
	"testing"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/types"
	xkms "github.com/jeremyhahn/trusted-firmware/xkms"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Compile-time proof the AVR aliases ARE the upstream types rather than
// lookalikes, following the peer and DICE surfaces.
//
// A lookalike compiles, passes every test in this package, and fails at the one
// place it matters: a consumer handing a value across the SDK boundary into the
// canonical implementation. Assignability in both directions rules that out.
var (
	_ avr.Config                = xkms.AVRConfig{}
	_ xkms.AVRConfig            = avr.Config{}
	_ avr.AttestationConfig     = xkms.AVRAttestationConfig{}
	_ xkms.AVRAttestationConfig = avr.AttestationConfig{}
	_ avr.Attestation           = xkms.AVRAttestation{}
	_ xkms.AVRAttestation       = avr.Attestation{}
	_ avr.Identity              = xkms.AVRIdentity{}
	_ xkms.AVRIdentity          = avr.Identity{}
	_ avr.Health                = xkms.AVRHealth{}
	_ xkms.AVRHealth            = avr.Health{}
	_ avr.Algorithms            = xkms.AVRAlgorithms{}
	_ xkms.AVRAlgorithms        = avr.Algorithms{}
	_ *avr.Backend              = (*xkms.AVRBackend)(nil)
	_ *xkms.AVRBackend          = (*avr.Backend)(nil)
	_ *avr.DeviceKey            = (*xkms.AVRDeviceKey)(nil)
	_ *xkms.AVRDeviceKey        = (*avr.DeviceKey)(nil)
	_ avr.Algorithm             = xkms.AVRAlgorithmECDSAP256SHA256
	_ xkms.AVRAlgorithm         = avr.AlgorithmEd25519
)

// TestAVRBackendIsConstructableFromTheSDK is the finding this file answers.
//
// BackendTypeAVR was exported on its own: a label for a capability with no way
// to configure or construct it, which left a consumer needing
// go-xkms/pkg/backend/avr -- an import go-trusted-platform's layer guard
// refuses outright. Reaching the constructor and its refusals through the SDK
// is what makes the backend usable rather than merely named.
func TestAVRBackendIsConstructableFromTheSDK(t *testing.T) {
	require.NotNil(t, xkms.NewAVRBackend)
	require.NotNil(t, xkms.DefaultAVRConfig)

	config := xkms.DefaultAVRConfig()
	require.NotNil(t, config)
	assert.Equal(t, xkms.AVRDefaultKeyCN, config.KeyCN)
	assert.Equal(t, xkms.AVRDefaultRequestTimeout, config.RequestTimeout)

	// The enum value and the backend it names are reachable from the same
	// place, which was the whole complaint.
	assert.Equal(t, xkms.AVRBackendType, (&avr.Backend{}).Type())
	assert.Equal(t, types.BackendType("tfw"), xkms.AVRBackendType)

	// A refusal the constructor makes, classified through the SDK's sentinel
	// rather than through pkg/.
	backend, err := xkms.NewAVRBackend(context.Background(), nil, nil)
	require.Error(t, err)
	assert.Nil(t, backend)
	assert.ErrorIs(t, err, xkms.ErrAVRNilConfig)

	backend, err = xkms.NewAVRBackend(context.Background(), config, nil)
	require.Error(t, err)
	assert.Nil(t, backend)
	assert.ErrorIs(t, err, xkms.ErrAVRNilDevice)
}

// TestAVRErrorsAreTheCanonicalSentinels pins identity rather than equality, for
// the reason the DICE surface does: an alias to a copy compiles and never
// matches under errors.Is.
func TestAVRErrorsAreTheCanonicalSentinels(t *testing.T) {
	pairs := []struct {
		name string
		sdk  error
		pkg  error
	}{
		{"nil config", xkms.ErrAVRNilConfig, avr.ErrNilConfig},
		{"nil device", xkms.ErrAVRNilDevice, avr.ErrNilDevice},
		{"invalid factory config", xkms.ErrAVRInvalidFactoryConfig, avr.ErrInvalidFactoryConfig},
		{"missing key cn", xkms.ErrAVRMissingKeyCN, avr.ErrMissingKeyCN},
		{"invalid slot", xkms.ErrAVRInvalidSlot, avr.ErrInvalidSlot},
		{"backend closed", xkms.ErrAVRBackendClosed, avr.ErrBackendClosed},
		{"invalid key attributes", xkms.ErrAVRInvalidKeyAttributes, avr.ErrInvalidKeyAttributes},
		{"unknown key", xkms.ErrAVRUnknownKey, avr.ErrUnknownKey},
		{"invalid nonce", xkms.ErrAVRInvalidNonce, avr.ErrInvalidNonce},
		{"trust store init", xkms.ErrAVRTrustStoreInit, avr.ErrTrustStoreInit},
		{"signing unsupported", xkms.ErrAVRSigningUnsupported, avr.ErrSigningUnsupported},
		{"invalid attestation", xkms.ErrAVRInvalidAttestation, avr.ErrInvalidAttestation},
		{"possession mismatch", xkms.ErrAVRPossessionMismatch, avr.ErrPossessionMismatch},
		{"decryption unsupported", xkms.ErrAVRDecryptionUnsupported, avr.ErrDecryptionUnsupported},
		{"generation unsupported", xkms.ErrAVRGenerationUnsupported, avr.ErrGenerationUnsupported},
		{"deletion unsupported", xkms.ErrAVRDeletionUnsupported, avr.ErrDeletionUnsupported},
		{"rotation unsupported", xkms.ErrAVRRotationUnsupported, avr.ErrRotationUnsupported},
		{"malformed certificate", xkms.ErrAVRMalformedCertificate, avr.ErrMalformedCertificate},
		{"protocol version", xkms.ErrAVRProtocolVersion, avr.ErrProtocolVersion},
		{"unknown crypto tier", xkms.ErrAVRUnknownCryptoTier, avr.ErrUnknownCryptoTier},
		{"no device key", xkms.ErrAVRNoDeviceKey, avr.ErrNoDeviceKey},
		{"invalid device point", xkms.ErrAVRInvalidDevicePoint, avr.ErrInvalidDevicePoint},
		{"device unreachable", xkms.ErrAVRDeviceUnreachable, avr.ErrDeviceUnreachable},
		{"device protocol", xkms.ErrAVRDeviceProtocol, avr.ErrDeviceProtocol},
		{"device refused", xkms.ErrAVRDeviceRefused, avr.ErrDeviceRefused},
		{"command unavailable", xkms.ErrAVRCommandUnavailable, avr.ErrCommandUnavailable},
		{"not provisioned", xkms.ErrAVRNotProvisioned, avr.ErrNotProvisioned},
		{"device crypto", xkms.ErrAVRDeviceCrypto, avr.ErrDeviceCrypto},
		{"auth required", xkms.ErrAVRAuthRequired, avr.ErrAuthRequired},
		{"device busy", xkms.ErrAVRDeviceBusy, avr.ErrDeviceBusy},
		{"protected region", xkms.ErrAVRProtectedRegion, avr.ErrProtectedRegion},
		{"device internal", xkms.ErrAVRDeviceInternal, avr.ErrDeviceInternal},
		{"unsupported by device", xkms.ErrAVRUnsupportedByDevice, avr.ErrUnsupportedByDevice},
		{"element unreachable", xkms.ErrAVRElementUnreachable, avr.ErrElementUnreachable},
		{"invalid argument", xkms.ErrAVRInvalidArgument, avr.ErrInvalidArgument},
	}

	for _, pair := range pairs {
		t.Run(pair.name, func(t *testing.T) {
			assert.Same(t, pair.pkg, pair.sdk, "the SDK re-exports a copy rather than the sentinel")
		})
	}
}

// The format string is the one the ACME challenge uses for the same evidence.
// Two names for one format is how a caller routing on it ends up with a branch
// that never runs.
func TestAVRFormatMatchesTheChallengeFormat(t *testing.T) {
	assert.Equal(t, deviceattest01.FormatDICE.String(), xkms.AVRFormatDICE)
	assert.Equal(t, "dice", xkms.AVRFormatDICE)
}

func TestEveryAVRAlgorithmAliasNamesItsOwnAlgorithm(t *testing.T) {
	assert.Equal(t, avr.AlgorithmECDSAP256SHA256, xkms.AVRAlgorithmECDSAP256SHA256)
	assert.Equal(t, avr.AlgorithmEd25519, xkms.AVRAlgorithmEd25519)
	assert.Equal(t, avr.AlgorithmHMACSHA256, xkms.AVRAlgorithmHMACSHA256)

	assert.Equal(t, xkms.AVRAlgorithm("ecdsa-p256-sha256"), xkms.AVRAlgorithmECDSAP256SHA256)
	assert.Equal(t, xkms.AVRAlgorithm("ed25519"), xkms.AVRAlgorithmEd25519)
	assert.Equal(t, xkms.AVRAlgorithm("hmac-sha256"), xkms.AVRAlgorithmHMACSHA256)
}
