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
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// A config entry that is present but nil is the same as one left out: the
// defaults apply. A map built from optional settings carries a nil for the
// setting nobody gave, and that is not a wrong type.
func TestNewKeyProviderTreatsANilConfigEntryAsAbsent(t *testing.T) {
	kp, err := NewKeyProvider(map[string]interface{}{
		ConfigKeyDevice: newTestDevice(secureElementLink()),
		ConfigKeyConfig: nil,
	})
	require.NoError(t, err)
	t.Cleanup(func() { _ = kp.Close() })

	assert.Equal(t, DefaultKeyCN, kp.(*Backend).config.KeyCN)
}
