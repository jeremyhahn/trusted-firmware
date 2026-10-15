// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package xkms

import (
	"github.com/jeremyhahn/trusted-firmware/xkms/avr"

	goxkms "github.com/jeremyhahn/go-xkms/pkg/xkms"
)

// AVRBackendType is the go-xkms backend type the AVR backend registers under:
// "tfw". go-xkms keeps "avr" for its own built-in backend until go-xkms#1439
// step 3, so Key IDs written as "avr:..." do not resolve here.
const AVRBackendType = avr.BackendType

// The keys of the AVR backend's entry in AutoConfig.BackendConfigs["tfw"].
const (
	// AVRConfigKeyDevice is the attached *tfw.Device. Required.
	AVRConfigKeyDevice = avr.ConfigKeyDevice

	// AVRConfigKeyConfig is the backend's *AVRConfig. Optional.
	AVRConfigKeyConfig = avr.ConfigKeyConfig
)

// init registers the AVR backend with go-xkms, so importing this package --
// blank, if nothing else is needed from it -- makes "tfw" a backend type
// AutoInitialize constructs from AutoConfig.BackendConfigs["tfw"].
//
// A refused registration panics. It means another module already holds the
// name, and the alternative is a process that starts with the backend silently
// missing and fails later, on a device, at a distance from the cause.
func init() {
	if err := RegisterAVRBackend(); err != nil {
		panic(err)
	}
}

// RegisterAVRBackend registers the AVR backend with go-xkms's registry hook.
// init has already called it; a second call is refused with an error that
// satisfies errors.Is(err, types.ErrBackendTypeRegistered), as go-xkms refuses
// every name registered twice.
func RegisterAVRBackend() error {
	return goxkms.RegisterExternalBackend(goxkms.ExternalBackend{
		Type:    goxkms.BackendType(avr.BackendType),
		Factory: avr.NewKeyProvider,
	})
}
