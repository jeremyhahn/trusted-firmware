// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

// Package registration_test pins what a consumer gets from a BLANK import of
// the adapter module and nothing else: the AVR backend registered with
// go-xkms under "tfw", constructed by AutoInitialize from its BackendConfigs
// entry, and the name held against a second claim.
//
// It is its own package so the import below is the only one that could have
// registered anything.
package registration_test

import (
	"errors"
	"io"
	"log/slog"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"

	"github.com/jeremyhahn/go-xkms/pkg/types"
	goxkms "github.com/jeremyhahn/go-xkms/pkg/xkms"

	_ "github.com/jeremyhahn/trusted-firmware/xkms"
)

const tfwType = "tfw"

func TestBlankImportMakesTFWABackendType(t *testing.T) {
	assert.True(t, goxkms.BackendType(tfwType).IsValid())
	assert.True(t, goxkms.IsBackendSupported(tfwType))
	assert.Contains(t, goxkms.SupportedBackends(), goxkms.BackendType(tfwType))

	_, err := goxkms.RequireBackend(tfwType)
	assert.NoError(t, err)

	// The type is also the store type of every key the backend holds, and a
	// Key ID naming it parses.
	assert.True(t, types.StoreType(tfwType).IsValid())
	assert.Equal(t, types.StoreType(tfwType), types.ParseStoreType(tfwType))
	backend, _, _, _, err := goxkms.ParseKeyID("tfw:idevid:ecdsa-p256:device-identity")
	require.NoError(t, err)
	assert.Equal(t, tfwType, backend)
}

func TestAutoInitializeConstructsTheAVRBackend(t *testing.T) {
	device := tfwsim.StartElement(t)
	config := avr.DefaultConfig()
	config.Logger = slog.New(slog.NewTextHandler(io.Discard, nil))

	goxkms.Reset()
	t.Cleanup(func() {
		_ = goxkms.Close()
		goxkms.Reset()
	})

	require.NoError(t, goxkms.AutoInitialize(&goxkms.AutoConfig{
		BackendConfigs: map[goxkms.BackendType]map[string]interface{}{
			tfwType: {
				avr.ConfigKeyDevice: device,
				avr.ConfigKeyConfig: config,
			},
		},
	}))

	backend, err := goxkms.GetBackend(tfwType)
	require.NoError(t, err, "AutoInitialize did not construct the tfw backend")
	require.NotNil(t, backend.KeyProvider())

	assert.Equal(t, types.BackendType(tfwType), backend.KeyProvider().Type())
	assert.IsType(t, &avr.Backend{}, backend.KeyProvider())
	assert.Equal(t, types.StoreType(tfwType), backend.Type())
}

// The name is registered once per process. A second claim -- the adapter's
// own, or anyone else's -- is refused and changes nothing.
func TestASecondRegistrationIsRefused(t *testing.T) {
	err := goxkms.RegisterExternalBackend(goxkms.ExternalBackend{
		Type:    tfwType,
		Factory: avr.NewKeyProvider,
	})
	require.Error(t, err)
	assert.True(t, errors.Is(err, types.ErrBackendTypeRegistered), "got %v", err)

	var refused *types.BackendTypeRegistrationError
	require.ErrorAs(t, err, &refused)
	assert.Equal(t, types.BackendType(tfwType), refused.Type)

	assert.True(t, goxkms.BackendType(tfwType).IsValid(), "the refusal unregistered the name")
}
