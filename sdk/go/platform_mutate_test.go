// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Mutation tests for platform.go: each test kills a fault the other tests
// let through. Faults applied (killed/total in the commit): change the magic,
// the layout version, or any field offset by one, CRC one byte short, write
// the CRC or the magic big-endian, skip any field's copy, skip the layout
// byte, write into the reserved span.

// The block is laid out byte for byte as the firmware reads it. Offsets,
// magic and version are literals here, so a change to the package's own
// constants is a failure, not a moved expectation.
func TestMutatePlatformBlockLayoutIsPinned(t *testing.T) {
	var serial [SerialLen]byte
	var hash [PubkeyHashLen]byte
	var op [OperatorPubkeyLen]byte
	for i := range serial {
		serial[i] = byte(0x10 + i)
	}
	for i := range hash {
		hash[i] = byte(0x40 + i)
	}
	for i := range op {
		op[i] = byte(0x80 + i)
	}

	got := BuildPlatformBlock(serial, hash, op)
	require.Len(t, got, 192)

	want := make([]byte, 192)
	copy(want[0:4], []byte{'T', 'F', 'P', 'R'})
	want[4] = 2
	copy(want[8:40], hash[:])
	copy(want[52:68], serial[:])
	copy(want[68:132], op[:])
	crc := CRC16(want[:190])
	want[190] = byte(crc)
	want[191] = byte(crc >> 8)

	assert.Equal(t, want, got[:])
}

// Every byte the block does not take an argument for is zero: rollback
// floor, boot count and state, OTA state, fail count and the reserved span.
func TestMutatePlatformBlockUnsetFieldsAreZero(t *testing.T) {
	var serial [SerialLen]byte
	var hash [PubkeyHashLen]byte
	var op [OperatorPubkeyLen]byte
	for i := range serial {
		serial[i] = 0xFF
	}
	for i := range hash {
		hash[i] = 0xFF
	}
	for i := range op {
		op[i] = 0xFF
	}
	got := BuildPlatformBlock(serial, hash, op)
	for _, span := range [][2]int{{5, 8}, {40, 52}, {132, 190}} {
		for i := span[0]; i < span[1]; i++ {
			assert.Zero(t, got[i], "byte %d", i)
		}
	}
}
