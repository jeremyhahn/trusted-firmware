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

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/stretchr/testify/assert"
)

// The attach line is what an operator reads to tell which protocol and which
// firmware a device runs, so each number is in its own place. Distinct values
// throughout, so a swapped or repeated field cannot pass.
func TestAttachLineVersionsKeepEachFieldInItsPlace(t *testing.T) {
	caps := tfw.Caps{
		ProtoMajor:    1,
		ProtoMinor:    2,
		FirmwareMajor: 3,
		FirmwareMinor: 4,
		FirmwarePatch: 5,
	}

	assert.Equal(t, "1.2", protocolString(caps))
	assert.Equal(t, "3.4.5", firmwareString(caps))
}
