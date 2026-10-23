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
	"testing"

	xkms "github.com/jeremyhahn/trusted-firmware/xkms"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/assert"
)

// Every re-exported refusal is the avr package's own sentinel, not a
// neighbour's: a consumer classifying with errors.Is through the SDK must reach
// the same answer it would through avr. The table is the whole var block, so an
// alias pointed at the wrong sentinel cannot hide behind one the older test
// does not list.
func TestEveryAVRErrorAliasIsItsOwnSentinel(t *testing.T) {
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
			assert.Same(t, pair.pkg, pair.sdk, "the alias points at another sentinel")
		})
	}
}

// Each algorithm alias names its own algorithm. The strings are literals so a
// swap of two aliases, which keeps every value in the vocabulary, still fails.
func TestEveryAVRAlgorithmAliasNamesItsOwnAlgorithm(t *testing.T) {
	assert.Equal(t, avr.AlgorithmECDSAP256SHA256, xkms.AVRAlgorithmECDSAP256SHA256)
	assert.Equal(t, avr.AlgorithmEd25519, xkms.AVRAlgorithmEd25519)
	assert.Equal(t, avr.AlgorithmHMACSHA256, xkms.AVRAlgorithmHMACSHA256)

	assert.Equal(t, xkms.AVRAlgorithm("ecdsa-p256-sha256"), xkms.AVRAlgorithmECDSAP256SHA256)
	assert.Equal(t, xkms.AVRAlgorithm("ed25519"), xkms.AVRAlgorithmEd25519)
	assert.Equal(t, xkms.AVRAlgorithm("hmac-sha256"), xkms.AVRAlgorithmHMACSHA256)
}
