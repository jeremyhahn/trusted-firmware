// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestNewKeyProviderConstructsTheBackendFromTheConfigMap(t *testing.T) {
	config := testConfig()
	kp, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: newTestDevice(secureElementLink()),
		ConfigKeyConfig: config,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = kp.Close() })

	b, ok := kp.(*Backend)
	require.True(t, ok, "the factory builds a %T", kp)
	assert.Equal(t, BackendType, b.Type())
	assert.Same(t, config, b.config, "the configured Config is the one used")
}

func TestNewKeyProviderDefaultsTheConfig(t *testing.T) {
	kp, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: newTestDevice(secureElementLink()),
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = kp.Close() })

	assert.Equal(t, DefaultKeyCN, kp.(*Backend).config.KeyCN)
}

// Ignoring "key_storage" is the documented behaviour: AutoInitialize adds it
// whenever a datastore is configured, and the device stores its own key.
func TestNewKeyProviderIgnoresKeyStorage(t *testing.T) {
	kp, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: newTestDevice(secureElementLink()),
		ConfigKeyConfig: testConfig(),
		"key_storage":   struct{}{},
	})
	require.NoError(t, err)
	require.NoError(t, kp.Close())
}

func TestNewKeyProviderRefusesAMissingDevice(t *testing.T) {
	for name, config := range map[string]map[string]interface{}{
		"nil map":    nil,
		"no device":  {ConfigKeyConfig: testConfig()},
		"nil device": {ConfigKeyDevice: nil},
	} {
		t.Run(name, func(t *testing.T) {
			kp, err := NewKeyProvider(config)

			assert.Nil(t, kp)
			assert.ErrorIs(t, err, ErrNilDevice)
		})
	}
}

// A value of the wrong type under a known key is refused, never ignored: a
// Config that was set and not used is a backend running with settings nobody
// chose.
func TestNewKeyProviderRefusesTheWrongTypes(t *testing.T) {
	for name, config := range map[string]map[string]interface{}{
		"device":       {ConfigKeyDevice: "/dev/ttyUSB0"},
		"config":       {ConfigKeyDevice: newTestDevice(secureElementLink()), ConfigKeyConfig: *testConfig()},
		"config value": {ConfigKeyDevice: newTestDevice(secureElementLink()), ConfigKeyConfig: "default"},
	} {
		t.Run(name, func(t *testing.T) {
			kp, err := NewKeyProvider(config)

			assert.Nil(t, kp)
			assert.ErrorIs(t, err, ErrInvalidFactoryConfig)
		})
	}
}

// What the constructor refuses, the factory refuses the same way.
func TestNewKeyProviderPassesConstructorRefusalsThrough(t *testing.T) {
	config := testConfig()
	config.KeyCN = ""

	kp, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: newTestDevice(secureElementLink()),
		ConfigKeyConfig: config,
	})

	assert.Nil(t, kp)
	assert.ErrorIs(t, err, ErrMissingKeyCN)
}
