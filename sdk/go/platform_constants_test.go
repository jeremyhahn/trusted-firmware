// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// platformBootH is the bootloader header that owns the EEPROM platform block
// layout platform.go copies. Paths are relative to sdk/go, the package
// directory go test runs in.
const platformBootH = "../../include/tfw/boot.h"

// platformBakedVectorC is the C unit test whose baked 192-byte block the
// bootloader's parser decodes.
const platformBakedVectorC = "../../test/unit/boot/test_boot_platform.c"

// platformConstant pairs a boot.h macro with the Go value that copies it.
type platformConstant struct {
	macro string
	got   uint64
}

func TestPlatformConstantsMatchBootH(t *testing.T) {
	src, err := os.ReadFile(platformBootH)
	require.NoError(t, err)

	platformDefine := func(name string) uint64 {
		v, ok := protoHeaderDefine(src, name)
		if !ok {
			t.Fatalf("%s: #define %s not found; the SDK copy of the platform block cannot be checked", platformBootH, name)
		}
		return v
	}

	platformConstants := []platformConstant{
		{"TFW_BOOT_EE_MAGIC", uint64(platformMagic)},
		{"TFW_BOOT_EE_VERSION", uint64(platformVersion)},
		{"TFW_BOOT_EE_BLOCK_LEN", PlatformBlockLen},
		{"TFW_BOOT_EE_O_MAGIC", platformOffMagic},
		{"TFW_BOOT_EE_O_LAYOUT", platformOffLayout},
		{"TFW_BOOT_EE_O_PUBKEY_HASH", platformOffPubkeyHash},
		{"TFW_BOOT_EE_O_SERIAL", platformOffSerial},
		{"TFW_BOOT_EE_O_OPERATOR_PUBKEY", platformOffOperatorPubkey},
		{"TFW_BOOT_EE_O_CRC", platformOffCRC},
		{"TFW_BOOT_SERIAL_LEN", SerialLen},
		{"TFW_BOOT_OPERATOR_PUBKEY_LEN", OperatorPubkeyLen},
		{"TFW_BOOT_EE_LEN", EEPROMReservedEnd},
	}
	for _, c := range platformConstants {
		assert.Equalf(t, platformDefine(c.macro), c.got,
			"%s in %s and the SDK's copy differ", c.macro, platformBootH)
	}

	// boot.h has no hash-length define: the pubkey hash runs up to the
	// rollback floor.
	platformHashLen := platformDefine("TFW_BOOT_EE_O_ROLLBACK_FLOOR") -
		platformDefine("TFW_BOOT_EE_O_PUBKEY_HASH")
	assert.Equal(t, platformHashLen, uint64(PubkeyHashLen),
		"TFW_BOOT_EE_O_ROLLBACK_FLOOR - TFW_BOOT_EE_O_PUBKEY_HASH and PubkeyHashLen differ")

	// The CRC is the block's last two bytes.
	assert.Equal(t, PlatformBlockLen, platformOffCRC+2)
}

// platformBakedByte is one byte of the C test's baked block.
var platformBakedByte = regexp.MustCompile(`0x([0-9A-Fa-f]{2})u`)

func TestBuildPlatformBlockWritesTheVectorTheBootParserDecodes(t *testing.T) {
	src, err := os.ReadFile(platformBakedVectorC)
	require.NoError(t, err)
	text := string(src)

	const platformBakedFunc = "test_a_baked_eeprom_image_is_decoded_correctly"
	const platformBakedArray = "static const uint8_t raw[TFW_BOOT_EE_BLOCK_LEN] = {"
	fn := strings.Index(text, platformBakedFunc)
	if fn < 0 {
		t.Fatalf("%s: function %s not found", platformBakedVectorC, platformBakedFunc)
	}
	arr := strings.Index(text[fn:], platformBakedArray)
	if arr < 0 {
		t.Fatalf("%s: %q not found after %s", platformBakedVectorC, platformBakedArray, platformBakedFunc)
	}
	body := text[fn+arr+len(platformBakedArray):]
	end := strings.Index(body, "}")
	if end < 0 {
		t.Fatalf("%s: the baked array in %s has no closing brace", platformBakedVectorC, platformBakedFunc)
	}

	var want []byte
	for _, m := range platformBakedByte.FindAllStringSubmatch(body[:end], -1) {
		b, err := strconv.ParseUint(m[1], 16, 8)
		require.NoError(t, err)
		want = append(want, byte(b))
	}
	require.Len(t, want, PlatformBlockLen, "the baked block in %s", platformBakedVectorC)

	var serial [SerialLen]byte
	for i := range serial {
		serial[i] = byte(i)
	}
	var hash [PubkeyHashLen]byte
	var op [OperatorPubkeyLen]byte
	got := BuildPlatformBlock(serial, hash, op)
	require.Equal(t, want, got[:],
		"BuildPlatformBlock no longer writes the block %s decodes", platformBakedVectorC)
}

func TestPlatformHeaderDefineSeesADrift(t *testing.T) {
	platformHeader := []byte("/* drifted */\n" +
		"#define TFW_BOOT_EE_VERSION 3u\n" +
		"#define TFW_BOOT_EE_MAGIC 0x52504654uL   /* 'T','F','P','R' */\n")

	v, ok := protoHeaderDefine(platformHeader, "TFW_BOOT_EE_VERSION")
	require.True(t, ok)
	assert.Equal(t, uint64(3), v)
	assert.NotEqual(t, uint64(platformVersion), v, "a drifted version must not match the SDK's")

	v, ok = protoHeaderDefine(platformHeader, "TFW_BOOT_EE_MAGIC")
	require.True(t, ok)
	assert.Equal(t, uint64(0x52504654), v)

	_, ok = protoHeaderDefine(platformHeader, "TFW_BOOT_EE_O_CRC")
	assert.False(t, ok)
}
