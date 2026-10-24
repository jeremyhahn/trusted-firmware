// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	deviceattest01 "github.com/jeremyhahn/go-xkms/pkg/acme/challenges/device-attest-01"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// The field link's behaviour without a simulator (row device-onboarding,
// docs/device-onboarding/architecture.md steps 9-15). The fake link answers
// only CAPS and ATTEST_CERT, which is what the Ethernet allow-list admits
// (src/proto/link_eth.c, tfw_link_admits); every other command gets the
// UNKNOWN_CMD a fielded device's link refuses it with. fieldlink_test.go,
// behind the tfwsim tag, drives the same steps against the real firmware.

// fieldLinkBackend builds the backend through the registry's factory over a
// device on link, as go-quicbaas builds it over an accepted connection.
func fieldLinkBackend(t *testing.T, device *tfw.Device) *Backend {
	t.Helper()
	provider, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: device,
		ConfigKeyConfig: testConfig(),
	})
	require.NoError(t, err)
	b, ok := provider.(*Backend)
	require.True(t, ok, "the factory returned a %T", provider)
	t.Cleanup(func() { assert.NoError(t, b.Close()) })
	return b
}

// The bench calls the allow-list leaves out answer ErrCommandUnavailable and
// carry the device's status, and a refusal leaves the link in step: the
// attestation after them on the same link succeeds.
func TestFieldLinkUnitRefusesBenchCallsAndStaysInStep(t *testing.T) {
	nonce := deviceattest01.DICENonce("after-refusals")
	link := secureElementLink().answerAttestCert(newAliasDevice(t).mint(t, nonce))
	b := fieldLinkBackend(t, newTestDevice(link))
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	for _, tc := range []struct {
		name string
		call func() error
	}{
		{"Identity", func() error { _, err := b.Identity(ctx); return err }},
		{"Health", func() error { _, err := b.Health(ctx); return err }},
		{"GetKey", func() error { _, err := b.GetKey(ctx, attrs); return err }},
	} {
		err := tc.call()
		assert.ErrorIs(t, err, ErrCommandUnavailable, "field link: %s", tc.name)
		assert.ErrorIs(t, err, tfw.StatusErrUnknownCmd, "field link: %s carries the device's status", tc.name)
	}

	result, err := b.AttestKey(ctx, attrs, nonce)
	require.NoError(t, err)
	att, ok := result.(*Attestation)
	require.True(t, ok, "AttestKey returned a %T", result)
	assert.Equal(t, nonce, att.Nonce)
}

// The backend never owns the link: closing it leaves the connection to the
// endpoint, which reads CAPS over it and builds the next backend on it.
func TestFieldLinkUnitCloseLeavesTheConnection(t *testing.T) {
	nonce := deviceattest01.DICENonce("next-backend")
	device := newTestDevice(secureElementLink().answerAttestCert(newAliasDevice(t).mint(t, nonce)))
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	closed := fieldLinkBackend(t, device)
	require.NoError(t, closed.Close())
	_, err := closed.AttestKey(ctx, attrs, nonce)
	assert.ErrorIs(t, err, ErrBackendClosed)

	caps, err := device.Caps(ctx)
	require.NoError(t, err, "the connection outlives the backend")
	assert.Equal(t, closed.Caps(), *caps)

	next := fieldLinkBackend(t, device)
	result, err := next.AttestKey(ctx, attrs, nonce)
	require.NoError(t, err)
	assert.Equal(t, nonce, result.(*Attestation).Nonce)
}
