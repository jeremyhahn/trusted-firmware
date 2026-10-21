// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

// The ids are wire values: a renumbering would make an old host read one board
// as another, so they are pinned as literals.
func TestBoardIDsAreThePublishedValues(t *testing.T) {
	assert.Equal(t, BoardID(0x01), BoardATmega328P)
	assert.Equal(t, BoardID(0x02), BoardATmega2560)
	assert.Equal(t, BoardID(0x03), BoardATmega32U4)
	assert.Equal(t, BoardID(0x04), BoardATtiny85)
	assert.Equal(t, BoardID(0x80), BoardESP32P4PoE)
	assert.Equal(t, BoardID(0x81), BoardESP32QEMU)
	assert.Equal(t, BoardID(0x40), BoardMPS2AN505)
}

func TestBoardIDStringNamesKnownBoards(t *testing.T) {
	assert.Equal(t, "atmega328p", BoardATmega328P.String())
	assert.Equal(t, "atmega2560", BoardATmega2560.String())
	assert.Equal(t, "atmega32u4", BoardATmega32U4.String())
	assert.Equal(t, "attiny85", BoardATtiny85.String())
	assert.Equal(t, "esp32p4-poe", BoardESP32P4PoE.String())
	assert.Equal(t, "esp32-qemu", BoardESP32QEMU.String())
	assert.Equal(t, "mps2-an505", BoardMPS2AN505.String())
	assert.Equal(t, "cortex-m", BoardMPS2AN505.Family())
	assert.Equal(t, "esp32/other", BoardESP32P4PoE.Family())
	assert.Equal(t, "esp32/other", BoardESP32QEMU.Family())
}

func TestBoardIDStringReportsTheFamilyOfAnUnknownID(t *testing.T) {
	assert.Equal(t, "unknown avr board 0x3f", BoardID(0x3F).String())
	assert.Equal(t, "unknown cortex-m board 0x41", BoardID(0x41).String())
	assert.Equal(t, "unknown esp32/other board 0x82", BoardID(0x82).String())
	assert.Equal(t, "unknown esp32/other board 0xbf", BoardID(0xBF).String())
	assert.Equal(t, "unknown unallocated board 0x00", BoardID(0x00).String())
	assert.Equal(t, "unknown unallocated board 0xc0", BoardID(0xC0).String())
}

func TestBoardIDFamilyRangesAreContiguous(t *testing.T) {
	for id, want := range map[BoardID]string{
		0x01: "avr", 0x3F: "avr", 0x40: "cortex-m", 0x7F: "cortex-m",
		0x80: "esp32/other", 0xBF: "esp32/other",
		0x00: "unallocated", 0xC0: "unallocated", 0xFF: "unallocated",
	} {
		assert.Equal(t, want, id.Family(), "0x%02x", uint8(id))
	}
}
