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

package avr

import (
	"crypto"
	"io"
	"log/slog"
	"testing"

	"github.com/jeremyhahn/go-xkms/pkg/types"
	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The key records the slot it was read from, whatever that slot is.
func TestNewDeviceKeyKeepsItsSlot(t *testing.T) {
	point, _ := testPoint(t)

	k, err := newDeviceKey(point, 5, DefaultKeyCN)
	require.NoError(t, err)
	assert.Equal(t, uint8(5), k.Slot())
}

// One point in two slots is two keys: Equal compares the slot as well as the
// point, so a key read from the wrong slot never passes for the identity.
func TestDeviceKeyEqualComparesTheSlot(t *testing.T) {
	point, _ := testPoint(t)

	a, err := newDeviceKey(point, tfw.SlotDeviceID, DefaultKeyCN)
	require.NoError(t, err)
	other, err := newDeviceKey(point, tfw.SlotDeviceID+1, DefaultKeyCN)
	require.NoError(t, err)

	assert.False(t, a.Equal(other))
}

// The key record states the hash the device signs with and that the key was
// generated on the part.
func TestKeyAttributesDescribeTheDeviceKey(t *testing.T) {
	b := &Backend{
		config: testConfig(),
		logger: slog.New(slog.NewTextHandler(io.Discard, nil)),
	}

	attrs := b.keyAttributes()

	assert.Equal(t, crypto.SHA256, attrs.Hash)
	assert.Equal(t, types.KeyOriginGenerated, attrs.KeyOrigin)
}
