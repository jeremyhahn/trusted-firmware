// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package xkms

import (
	"errors"
	"testing"

	"github.com/jeremyhahn/go-xkms/pkg/types"
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"
	"github.com/stretchr/testify/assert"
)

// The refusal init would meet if another module held "tfw" -- here go-xkms's
// own refusal of a second registration -- panics rather than starting a
// process with the backend missing.
func TestARefusedRegistrationPanics(t *testing.T) {
	refused := RegisterAVRBackend()
	if !errors.Is(refused, types.ErrBackendTypeRegistered) {
		t.Fatalf("second registration: err = %v, want %v", refused, types.ErrBackendTypeRegistered)
	}
	defer func() {
		if got := recover(); got != refused {
			t.Fatalf("recovered %v, want the refusal %v", got, refused)
		}
	}()
	mustRegister(refused)
	t.Fatal("mustRegister returned on a refused registration")
}

func TestAVRConfigKeysAreTheFactoryKeys(t *testing.T) {
	assert.Equal(t, avr.ConfigKeyDevice, AVRConfigKeyDevice)
	assert.Equal(t, avr.ConfigKeyConfig, AVRConfigKeyConfig)

	assert.Equal(t, "device", AVRConfigKeyDevice)
	assert.Equal(t, "config", AVRConfigKeyConfig)
}
