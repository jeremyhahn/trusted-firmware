// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"encoding/binary"
	"testing"
)

func testSerial() [SerialLen]byte {
	var s [SerialLen]byte
	for i := range s {
		s[i] = byte(0x30 + i)
	}
	return s
}

func TestBuildPlatformBlockLayout(t *testing.T) {
	serial := testSerial()
	var hash [PubkeyHashLen]byte
	for i := range hash {
		hash[i] = byte(i)
	}

	raw := BuildPlatformBlock(serial, hash)

	if got := binary.LittleEndian.Uint32(raw[platformOffMagic:]); got != platformMagic {
		t.Fatalf("magic = 0x%08x, want 0x%08x", got, platformMagic)
	}
	if raw[platformOffLayout] != platformVersion {
		t.Fatalf("layout_version = %d, want %d", raw[platformOffLayout], platformVersion)
	}
	if got := raw[platformOffPubkeyHash : platformOffPubkeyHash+PubkeyHashLen]; string(got) != string(hash[:]) {
		t.Fatalf("pubkey_hash mismatch")
	}
	if got := raw[platformOffSerial : platformOffSerial+SerialLen]; string(got) != string(serial[:]) {
		t.Fatalf("device_serial mismatch")
	}
	// boot_state, ota_state, boot_count, rollback_floor, boot_fail_count: all
	// zero on a factory-fresh block -- AG_BOOT_STATE_EMPTY is zero by
	// definition, and the counters simply have not happened yet.
	for i := 40; i < platformOffSerial; i++ {
		if raw[i] != 0 {
			t.Fatalf("byte %d = 0x%02x, want 0x00 (factory-fresh)", i, raw[i])
		}
	}
}

// The CRC must be the same algorithm and cover the same span
// boot/boot_platform.c's ag_boot_platform_crc computes -- a Go tool baking an
// image the firmware's own parser cannot verify would fail silently on the
// device, not here.
func TestBuildPlatformBlockCRCCoversThePrefix(t *testing.T) {
	serial := testSerial()
	var hash [PubkeyHashLen]byte

	raw := BuildPlatformBlock(serial, hash)
	want := CRC16(raw[:platformOffCRC])
	got := binary.LittleEndian.Uint16(raw[platformOffCRC:])
	if got != want {
		t.Fatalf("crc16 = 0x%04x, want 0x%04x", got, want)
	}
}

// A single flipped serial byte must change the CRC -- otherwise the field the
// whole mechanism exists to protect is not actually covered.
func TestBuildPlatformBlockCRCDetectsATamperedSerial(t *testing.T) {
	serial := testSerial()
	var hash [PubkeyHashLen]byte

	a := BuildPlatformBlock(serial, hash)
	serial[0] ^= 0xFF
	b := BuildPlatformBlock(serial, hash)

	if binary.LittleEndian.Uint16(a[platformOffCRC:]) == binary.LittleEndian.Uint16(b[platformOffCRC:]) {
		t.Fatal("changing the serial did not change the CRC")
	}
}
