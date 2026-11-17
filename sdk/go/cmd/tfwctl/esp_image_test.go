//go:build tfwsim

package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The ESP32 stager is ESP-IDF's esp_ota_* (src/hal/esp32/hal_ota.c), which
// takes an ESP-IDF app image and nothing else: esp_ota_write refuses a first
// chunk without the 0xE9 magic and esp_ota_end refuses an image
// esp_image_verify does not take, both as ESP_ERR_OTA_VALIDATE_FAILED, which
// the port answers as TFW_ERR_CRYPTO (#562). So an ESP32 row stages the
// smallest image the verifier takes, followed by the TFIM trailer where
// check_staged reads it (at the image's end, the appended SHA-256 its
// measurement).
const (
	espImgMagic     = 0xE9
	espImgHeaderLen = 24
	espSegHeaderLen = 8
	espChecksumSeed = 0xEF
	espHashLen      = 32

	espAppDescMagic    = 0xABCD5432
	espAppDescLen      = 256
	espAppDescEfuseRev = 176 // min_efuse_blk_rev_full, then max_
)

// espTargetEnv names the machine an esp32-qemu run emulates: hw-test passes
// the build's ESP_TARGET (boards/esp32-qemu/profile.mk), esp32c3 or esp32.
const espTargetEnv = "TFW_HW_ESP_TARGET"

// espChipID is the board's ESP-IDF chip id (esp_chip_id_t) on the given
// machine, and whether the board's stager is ESP-IDF's. esp32-qemu is one
// board on two machines, and ESP-IDF refuses an image for the other chip.
func espChipID(board tfw.BoardID, machine string) (uint16, bool) {
	switch board {
	case tfw.BoardESP32P4PoE:
		return 0x0012, true // ESP_CHIP_ID_ESP32P4
	case tfw.BoardESP32QEMU:
		if machine == "esp32" {
			return 0x0000, true // ESP_CHIP_ID_ESP32
		}
		return 0x0005, true // ESP_CHIP_ID_ESP32C3, profile.mk's default
	}
	return 0, false
}

// espAppImage is a total-byte image: an ESP-IDF app image of one body-byte
// padding segment (load address 0, which the loader neither maps nor loads),
// its checksum and appended SHA-256, then the 128-byte TFIM trailer naming
// board and measuring the image up to that hash, then 0xFF.
func espAppImage(total, body int, board tfw.BoardID, chip uint16) []byte {
	body = max(body, espAppDescLen) &^ 3 // a segment's length is a multiple of 4
	img := bytes.Repeat([]byte{0xFF}, total)
	h := img[:espImgHeaderLen]
	clear(h)
	h[0] = espImgMagic
	h[1] = 1    // segment count
	h[2] = 0x02 // DIO
	h[3] = 0x20 // 4 MB, 40 MHz: the verifier only warns on these
	h[8] = 0xEE // no WP pin
	binary.LittleEndian.PutUint16(h[12:], chip)
	binary.LittleEndian.PutUint16(h[15:], 0)      // min_chip_rev_full
	binary.LittleEndian.PutUint16(h[17:], 0xFFFF) // max_chip_rev_full
	h[23] = 1                                     // hash_appended
	seg := img[espImgHeaderLen : espImgHeaderLen+espSegHeaderLen]
	binary.LittleEndian.PutUint32(seg[0:], 0)
	binary.LittleEndian.PutUint32(seg[4:], uint32(body))
	off := espImgHeaderLen + espSegHeaderLen
	sum := byte(espChecksumSeed)
	for i := 0; i < body; i++ {
		img[off+i] = byte(i*7 + 3)
	}
	// ESP-IDF 5.5 reads esp_app_desc_t at the first segment's start and
	// checks its eFuse block revision range against the chip (boot_comm): a
	// descriptor with the magic, a 0..0xFFFF range and the rest zero.
	desc := img[off : off+espAppDescLen]
	clear(desc)
	binary.LittleEndian.PutUint32(desc[0:], espAppDescMagic)
	binary.LittleEndian.PutUint16(desc[espAppDescEfuseRev+2:], 0xFFFF)
	for i := 0; i < body; i++ {
		sum ^= img[off+i]
	}
	off += body
	for (off+1)%16 != 0 { // the checksum is the last byte of a 16-byte block
		img[off] = 0
		off++
	}
	img[off] = sum
	off++
	hash := sha256.Sum256(img[:off])
	copy(img[off:], hash[:])
	measured := off
	off += espHashLen
	tr := img[off : off+128]
	clear(tr)
	copy(tr, "TFIM")
	binary.LittleEndian.PutUint16(tr[4:], 1)
	tr[6] = byte(board)
	binary.LittleEndian.PutUint32(tr[8:], 3)
	binary.LittleEndian.PutUint32(tr[12:], uint32(measured))
	copy(tr[16:], hash[:])
	return img
}

// TestStageImageForESP32IsAnAppImage checks the image an ESP32 row stages
// against what ESP-IDF's esp_ota_write and esp_image_verify ask of it, and
// that the trailer sits where check_staged reads it.
func TestStageImageForESP32IsAnAppImage(t *testing.T) {
	for _, c := range []struct {
		board   tfw.BoardID
		machine string
		chip    uint16
	}{
		{tfw.BoardESP32P4PoE, "", 0x0012},
		{tfw.BoardESP32QEMU, "", 0x0005},
		{tfw.BoardESP32QEMU, "esp32c3", 0x0005},
		{tfw.BoardESP32QEMU, "esp32", 0x0000},
	} {
		t.Setenv(espTargetEnv, c.machine)
		board, chip := c.board, c.chip
		img := stageImageFor(512, 200, board)
		require.Len(t, img, 512)
		require.Equal(t, byte(espImgMagic), img[0], "esp_ota_write's first-byte check")
		require.Equal(t, byte(1), img[1], "one segment")
		assert.Equal(t, chip, binary.LittleEndian.Uint16(img[12:]), "chip id of %v on %q", board, c.machine)
		assert.Equal(t, byte(1), img[23], "hash_appended")
		segLen := int(binary.LittleEndian.Uint32(img[espImgHeaderLen+4:]))
		require.Zero(t, segLen%4)
		data := img[espImgHeaderLen+espSegHeaderLen : espImgHeaderLen+espSegHeaderLen+segLen]
		require.GreaterOrEqual(t, segLen, espAppDescLen)
		assert.Equal(t, uint32(espAppDescMagic), binary.LittleEndian.Uint32(data), "esp_app_desc_t magic")
		assert.Zero(t, binary.LittleEndian.Uint16(data[espAppDescEfuseRev:]), "min eFuse block rev")
		assert.Equal(t, uint16(0xFFFF), binary.LittleEndian.Uint16(data[espAppDescEfuseRev+2:]), "max eFuse block rev")
		sum := byte(espChecksumSeed)
		for _, b := range data {
			sum ^= b
		}
		end := espImgHeaderLen + espSegHeaderLen + segLen
		end += 15 - end%16 // the checksum byte's offset
		assert.Equal(t, sum, img[end], "checksum")
		end++
		hash := sha256.Sum256(img[:end])
		assert.Equal(t, hash[:], img[end:end+espHashLen], "appended SHA-256")
		tr := img[end+espHashLen:]
		require.GreaterOrEqual(t, len(tr), 128)
		assert.Equal(t, []byte("TFIM"), tr[:4])
		assert.Equal(t, byte(board), tr[6], "binding")
		assert.Equal(t, uint32(end), binary.LittleEndian.Uint32(tr[12:]), "measured length")
		assert.Equal(t, hash[:], tr[16:48], "measurement")
	}
}

// TestStageImageForOtherBoardsIsUnchanged: the core stager's image keeps
// its trailer in the last 128 bytes.
func TestStageImageForOtherBoardsIsUnchanged(t *testing.T) {
	img := stageImageFor(512, 200, tfw.BoardATmega328P)
	assert.NotEqual(t, byte(espImgMagic), img[0])
	assert.Equal(t, []byte("TFIM"), img[512-128:512-124])
}
