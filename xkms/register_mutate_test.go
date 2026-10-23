// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package xkms_test

import (
	"testing"

	xkms "github.com/jeremyhahn/trusted-firmware/xkms"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/assert"
)

// The config keys are what a consumer writes into
// AutoConfig.BackendConfigs["tfw"], and the factory reads the avr package's
// keys. Each alias must be its own key: swapped, the device would be read
// from the config entry and the factory would refuse a working setup. The
// literals pin the map keys a consumer may already have written.
func TestAVRConfigKeysAreTheFactoryKeys(t *testing.T) {
	assert.Equal(t, avr.ConfigKeyDevice, xkms.AVRConfigKeyDevice)
	assert.Equal(t, avr.ConfigKeyConfig, xkms.AVRConfigKeyConfig)

	assert.Equal(t, "device", xkms.AVRConfigKeyDevice)
	assert.Equal(t, "config", xkms.AVRConfigKeyConfig)
}
