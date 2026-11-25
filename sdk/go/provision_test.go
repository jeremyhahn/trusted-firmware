// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// provInfoReply builds the 38 bytes the device sends: four revision bytes,
// config block 0, then the two lock bytes.
//
// THIS FILE EXISTS BECAUSE ITS ABSENCE COST A PANIC. ProvInfo bounds-checked at
// 36 bytes and then indexed 90 and 91 for the lock bytes, which are in config
// block 2 and not block 0. MaxPayload is 64, so no reply the protocol can carry
// ever reached those indices -- every legal answer panicked, and nothing caught
// it because provision.go had no test at all. The firmware now sends the two
// bytes explicitly and this asserts the layout in both directions.
func provInfoReply(lockValue, lockConfig byte) []byte {
	p := make([]byte, 0, 38)
	p = append(p, 0x00, 0x00, 0x60, 0x02) // revision; 0x60 is the 608 family
	block0 := make([]byte, 32)
	// The serial number is split across bytes 0-3 and 8-12 of block 0. The
	// split is the element's, not ours, which is why the host reassembles it.
	copy(block0[0:4], []byte{0x01, 0x23, 0x45, 0x67})
	copy(block0[8:13], []byte{0x89, 0xAB, 0xCD, 0xEF, 0x01})
	p = append(p, block0...)
	return append(p, lockValue, lockConfig)
}

func TestProvInfoParsesTheElementIdentity(t *testing.T) {
	link := &cannedLink{payload: provInfoReply(0x55, 0x55)}

	info, err := New(link).ProvInfo(context.Background())
	require.NoError(t, err)

	assert.Equal(t, [4]byte{0x00, 0x00, 0x60, 0x02}, info.Revision)
	assert.Equal(t, [9]byte{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01}, info.Serial)
}

// 0x55 is UNLOCKED. Note the polarity: the value that looks like a flag being
// set is the zone being open, so an inverted test here would read a blank part
// as a provisioned one.
func TestProvInfoDecodesBothLockStates(t *testing.T) {
	for _, tc := range []struct {
		name                     string
		lockValue, lockConfig    byte
		wantData, wantConfigLock bool
	}{
		{"a blank part", 0x55, 0x55, false, false},
		{"config locked, data open", 0x55, 0x00, false, true},
		{"both locked -- a provisioned part", 0x00, 0x00, true, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			link := &cannedLink{payload: provInfoReply(tc.lockValue, tc.lockConfig)}
			info, err := New(link).ProvInfo(context.Background())
			require.NoError(t, err)
			assert.Equal(t, tc.wantData, info.DataLocked, "data zone")
			assert.Equal(t, tc.wantConfigLock, info.ConfigLocked, "config zone")
		})
	}
}

// The 36-byte reply an older firmware sends. It must be refused rather than
// read past -- reading past it is the defect this file was written for.
func TestProvInfoRefusesAReplyWithoutTheLockBytes(t *testing.T) {
	link := &cannedLink{payload: provInfoReply(0x55, 0x55)[:36]}

	_, err := New(link).ProvInfo(context.Background())
	require.Error(t, err)
	var short *ShortReplyError
	require.ErrorAs(t, err, &short)
	assert.Equal(t, 38, short.Need)
	assert.Equal(t, 36, short.Got)
}

// A production image has no provisioning handler at all -- the feature is gated
// by file selection, not by a runtime check -- so it answers "unknown command".
// That is a different fact from a device that has one and refused.
func TestProvInfoSurfacesAProductionImage(t *testing.T) {
	link := &cannedLink{status: StatusErrUnknownCmd}
	_, err := New(link).ProvInfo(context.Background())
	require.Error(t, err)
	assert.ErrorIs(t, err, StatusErrUnknownCmd)
}

func TestProvLockRefusesASuppressedSummaryCRC(t *testing.T) {
	link := &cannedLink{}
	// Zero asks the element to lock WITHOUT checking the zone holds what the
	// caller believes it holds. On a part with no erase and no reset, that
	// turns one transcription error in a template into scrap.
	err := New(link).ProvLock(context.Background(), ZoneConfig, 0)
	require.Error(t, err)
	var rangeErr *ArgRangeError
	assert.ErrorAs(t, err, &rangeErr)
}

func TestProvWriteConfigRefusesAWrongSizedBlock(t *testing.T) {
	link := &cannedLink{}
	d := New(link)
	for _, n := range []int{0, 31, 33} {
		require.Error(t, d.ProvWriteConfig(context.Background(), 2, make([]byte, n)))
	}
}
