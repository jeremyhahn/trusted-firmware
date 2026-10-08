// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import "encoding/binary"

// The platform-reserved EEPROM block, [0x000, 0x0C0) on the device --
// include/tfw/boot.h's "platform-reserved EEPROM" section is the wire contract
// this mirrors, and boot/boot_platform.c's tfw_boot_platform_serialise is the
// same layout on the firmware's side of it.
//
// There is deliberately no wire command that writes this region -- see
// EEPROMWrite's refusal below TFW_EEPROM_RESERVED_END, which is unconditional
// -- so the only way it is ever set is BuildPlatformBlock, baked into a .eep
// image and flashed with the part's own programmer before the device ever
// answers a command. See sdk/go/cmd/tfwprov's --bake-eeprom and
// docs/device-provisioning/ for the procedure this is one step of.
const (
	PlatformBlockLen  = 192
	SerialLen         = 16
	PubkeyHashLen     = 32
	OperatorPubkeyLen = 64

	platformMagic   uint32 = 0x52504654 // 'T','F','P','R' little-endian
	platformVersion uint8  = 2          // issue #4: +operator_pubkey

	platformOffMagic          = 0
	platformOffLayout         = 4
	platformOffPubkeyHash     = 8
	platformOffSerial         = 52
	platformOffOperatorPubkey = 68
	platformOffCRC            = 190
)

// BuildPlatformBlock serialises a factory-fresh platform block: the magic,
// the layout version, the given pubkey hash, device serial and operator
// public key, and a CRC over everything before it. Every field this function
// does not take an argument for -- rollback_floor, boot_count, boot_state,
// ota_state, boot_fail_count, and the reserved span -- is zero, which is the
// correct value for a part that has never been booted: TFW_BOOT_STATE_EMPTY is
// zero by definition, and the rest simply have not happened yet.
//
// pubkeyHash may be the zero value on an element-tier build, where
// boot/boot_policy.c's pin check is skipped entirely (the element itself
// holds the verifying key, never handed to the application) -- but NOT on a
// software-tier (BOOT_SIG=ed25519-donna) build, where an tfw-boot with no
// matching hash refuses every image, signed or not. See docs/secure-boot/.
//
// operatorPubkey is the AUTH_CHALLENGE/AUTH_RESPONSE session's trust anchor
// (issue #4), a P-256 point as X||Y -- element-tier only. It is the zero
// value on a soft-tier or CRYPTO=none image, where AUTH_CHALLENGE/
// AUTH_RESPONSE are not compiled in and nothing ever reads this field; see
// docs/security/threat-model.md.
func BuildPlatformBlock(serial [SerialLen]byte, pubkeyHash [PubkeyHashLen]byte,
	operatorPubkey [OperatorPubkeyLen]byte) [PlatformBlockLen]byte {
	var raw [PlatformBlockLen]byte

	binary.LittleEndian.PutUint32(raw[platformOffMagic:], platformMagic)
	raw[platformOffLayout] = platformVersion
	copy(raw[platformOffPubkeyHash:], pubkeyHash[:])
	copy(raw[platformOffSerial:], serial[:])
	copy(raw[platformOffOperatorPubkey:], operatorPubkey[:])

	crc := CRC16(raw[:platformOffCRC])
	binary.LittleEndian.PutUint16(raw[platformOffCRC:], crc)
	return raw
}
