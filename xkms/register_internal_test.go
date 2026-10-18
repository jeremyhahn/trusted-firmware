// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package xkms

import (
	"errors"
	"testing"

	"github.com/jeremyhahn/go-xkms/pkg/types"
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
