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
	"github.com/stretchr/testify/require"
)

// The statuses TestDeviceErrorMapsEveryNamedStatus does not list. Each says
// something different about what to do next, so each is pinned: a device that
// refused an argument is the caller's fault, not a refusal of unknown kind, and
// a silent element is not a silent device.
func TestDeviceErrorMapsTheRemainingStatuses(t *testing.T) {
	tests := []struct {
		status   tfw.Status
		sentinel error
	}{
		{tfw.StatusErrRange, ErrInvalidArgument},
		{tfw.StatusErrBadArgc, ErrInvalidArgument},
		{tfw.StatusErrNACK, ErrElementUnreachable},
		{tfw.StatusErrUnsupported, ErrUnsupportedByDevice},
	}

	for _, tt := range tests {
		t.Run(tt.status.String(), func(t *testing.T) {
			err := deviceError(tt.status)
			assert.ErrorIs(t, err, tt.sentinel)
			assert.NotErrorIs(t, err, ErrDeviceRefused, "a named status is never the generic refusal")
		})
	}
}

// A truncated frame is an unreadable answer, not a device that did not answer.
func TestDeviceErrorClassifiesATruncatedFrameAsProtocol(t *testing.T) {
	err := deviceError(tfw.ErrShortFrame)

	assert.ErrorIs(t, err, ErrDeviceProtocol)
	assert.NotErrorIs(t, err, ErrDeviceUnreachable)
}

// The log line names each zone's own state; a part half-way through
// provisioning must not read as the other half.
func TestIdentityStringNamesEachZone(t *testing.T) {
	id := Identity{Serial: "0123", ConfigLocked: true, DataLocked: false}

	assert.Equal(t, "element 0123 (config locked true, data locked false)", id.String())
}

// The identity owns its bytes: a caller that reuses the SDK's ElementInfo for
// the next read must not rewrite an identity already handed out.
func TestNewIdentityCopiesTheElementBytes(t *testing.T) {
	info := &tfw.ElementInfo{
		Revision: [4]byte{0x00, 0x00, 0x60, 0x03},
		Serial:   [9]byte{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xEE},
	}

	id := newIdentity(info)
	require.Equal(t, info.Serial[:], id.SerialBytes)
	require.Equal(t, info.Revision[:], id.Revision)

	info.Serial = [9]byte{}
	info.Revision = [4]byte{}

	assert.Equal(t, []byte{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xEE}, id.SerialBytes)
	assert.Equal(t, []byte{0x00, 0x00, 0x60, 0x03}, id.Revision)
	assert.Equal(t, "0123456789abcdefee", id.Serial)
}
