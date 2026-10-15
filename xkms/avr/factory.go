// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

import (
	"context"
	"fmt"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/jeremyhahn/go-xkms/pkg/errs"
	"github.com/jeremyhahn/go-xkms/pkg/types"
)

// The keys NewKeyProvider reads from the configuration map go-xkms's
// AutoInitialize hands it (AutoConfig.BackendConfigs["tfw"]).
const (
	// ConfigKeyDevice is the attached device, a *tfw.Device. Required. The
	// link is the caller's to open -- a serial port, a USB CDC node, a
	// gateway's socket -- because the backend has no business guessing which.
	ConfigKeyDevice = "device"

	// ConfigKeyConfig is the backend's *Config. Optional; absent selects
	// DefaultConfig.
	ConfigKeyConfig = "config"
)

// NewKeyProvider is the backend's go-xkms factory: it constructs a Backend
// from AutoInitialize's per-backend configuration map.
//
// "key_storage", which AutoInitialize adds when a datastore is configured, is
// not read. The device holds its one key in the element and there is nothing
// for the host to store.
func NewKeyProvider(config map[string]interface{}) (types.KeyProvider, error) {
	raw, ok := config[ConfigKeyDevice]
	if !ok || raw == nil {
		return nil, ErrNilDevice
	}
	device, ok := raw.(*tfw.Device)
	if !ok {
		return nil, &errs.Detail{
			Err:    ErrInvalidFactoryConfig,
			Detail: fmt.Sprintf("%q is a %T, not a *tfw.Device", ConfigKeyDevice, raw),
		}
	}

	backendConfig := DefaultConfig()
	if raw, ok := config[ConfigKeyConfig]; ok && raw != nil {
		c, ok := raw.(*Config)
		if !ok {
			return nil, &errs.Detail{
				Err:    ErrInvalidFactoryConfig,
				Detail: fmt.Sprintf("%q is a %T, not a *avr.Config", ConfigKeyConfig, raw),
			}
		}
		backendConfig = c
	}

	// NewBackend bounds the CAPS exchange with the configured request
	// timeout, so the background context adds no wait of its own.
	b, err := NewBackend(context.Background(), backendConfig, device)
	if err != nil {
		return nil, err
	}
	return b, nil
}
