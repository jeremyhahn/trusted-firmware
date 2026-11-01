// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func testSerial() [SerialLen]byte {
	var s [SerialLen]byte
	for i := range s {
		s[i] = byte(0x30 + i)
	}
	return s
}

func testOperatorPubkey() [OperatorPubkeyLen]byte {
	var k [OperatorPubkeyLen]byte
	for i := range k {
		k[i] = byte(0x80 + i)
	}
	return k
}

// The block is laid out byte for byte as the firmware reads it, and its CRC
// covers bytes 0..189 as boot/boot_platform.c's tfw_boot_platform_crc does.
// Offsets, magic and version are literals here, so a change to the package's
// own constants is a failure, not a moved expectation. Every input byte is
// non-zero, so the whole-block compare also proves every field the block takes
// no argument for (boot and OTA state, counters, the reserved span) is zero.
func TestBuildPlatformBlockLayoutIsPinned(t *testing.T) {
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

// A single flipped serial byte must change the CRC -- otherwise the field the
// whole mechanism exists to protect is not actually covered.
func TestBuildPlatformBlockCRCDetectsATamperedSerial(t *testing.T) {
	serial := testSerial()
	var hash [PubkeyHashLen]byte
	var opPubkey [OperatorPubkeyLen]byte

	a := BuildPlatformBlock(serial, hash, opPubkey)
	serial[0] ^= 0xFF
	b := BuildPlatformBlock(serial, hash, opPubkey)

	if binary.LittleEndian.Uint16(a[platformOffCRC:]) == binary.LittleEndian.Uint16(b[platformOffCRC:]) {
		t.Fatal("changing the serial did not change the CRC")
	}
}

// The same property, for the field issue #4 adds: a tampered operator public
// key is exactly what the reserved-region write refusal and the new
// authenticated-read gate exist to catch, and the CRC is the layer that
// detects an accidental (not attacker-controlled) corruption of it.
func TestBuildPlatformBlockCRCDetectsATamperedOperatorPubkey(t *testing.T) {
	serial := testSerial()
	var hash [PubkeyHashLen]byte
	opPubkey := testOperatorPubkey()

	a := BuildPlatformBlock(serial, hash, opPubkey)
	opPubkey[0] ^= 0xFF
	b := BuildPlatformBlock(serial, hash, opPubkey)

	if binary.LittleEndian.Uint16(a[platformOffCRC:]) == binary.LittleEndian.Uint16(b[platformOffCRC:]) {
		t.Fatal("changing the operator public key did not change the CRC")
	}
}
