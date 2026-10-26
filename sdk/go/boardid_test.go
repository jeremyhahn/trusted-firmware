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
	assert.Equal(t, BoardID(0x41), BoardFRDMIMX95M7)
	assert.Equal(t, BoardID(0x42), BoardMPS2AN500)
	assert.Equal(t, BoardID(0x43), BoardSTM32H573IDK)
	assert.Equal(t, BoardID(0x44), BoardRP2350Pico2W)
	assert.Equal(t, BoardID(0x45), BoardNRF54L15DK)
	assert.Equal(t, BoardID(0x46), BoardFRDMMCXN947)
}

func TestBoardIDStringNamesKnownBoards(t *testing.T) {
	assert.Equal(t, "atmega328p", BoardATmega328P.String())
	assert.Equal(t, "atmega2560", BoardATmega2560.String())
	assert.Equal(t, "atmega32u4", BoardATmega32U4.String())
	assert.Equal(t, "attiny85", BoardATtiny85.String())
	assert.Equal(t, "esp32p4-poe", BoardESP32P4PoE.String())
	assert.Equal(t, "esp32-qemu", BoardESP32QEMU.String())
	assert.Equal(t, "mps2-an505", BoardMPS2AN505.String())
	assert.Equal(t, "stm32h573i-dk", BoardSTM32H573IDK.String())
	assert.Equal(t, "cortex-m", BoardSTM32H573IDK.Family())
	assert.Equal(t, "rp2350-pico2w", BoardRP2350Pico2W.String())
	assert.Equal(t, "cortex-m", BoardMPS2AN505.Family())
	assert.Equal(t, "cortex-m", BoardRP2350Pico2W.Family())
	assert.Equal(t, "nrf54l15-dk", BoardNRF54L15DK.String())
	assert.Equal(t, "cortex-m", BoardNRF54L15DK.Family())
	assert.Equal(t, "frdm-mcxn947", BoardFRDMMCXN947.String())
	assert.Equal(t, "cortex-m", BoardFRDMMCXN947.Family())
	assert.Equal(t, "frdm-imx95-m7", BoardFRDMIMX95M7.String())
	assert.Equal(t, "cortex-m", BoardFRDMIMX95M7.Family())
	assert.Equal(t, "mps2-an500", BoardMPS2AN500.String())
	assert.Equal(t, "cortex-m", BoardMPS2AN500.Family())
	assert.Equal(t, "esp32/other", BoardESP32P4PoE.Family())
	assert.Equal(t, "esp32/other", BoardESP32QEMU.Family())
}

func TestBoardIDStringReportsTheFamilyOfAnUnknownID(t *testing.T) {
	assert.Equal(t, "unknown avr board 0x3f", BoardID(0x3F).String())
	assert.Equal(t, "unknown cortex-m board 0x7f", BoardID(0x7F).String())
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
