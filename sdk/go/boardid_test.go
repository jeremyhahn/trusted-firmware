// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"fmt"
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
	assert.Equal(t, BoardID(0x47), BoardNUCLEOH563ZI)
}

// TestBoardIDEveryValue: each of the 256 board ids names its own board or
// reads as unknown with its own value, never as another board; the family
// is the range the id sits in: 0x01-0x3F AVR, 0x40-0x7F Cortex-M, 0x80-0xBF
// ESP32 and other, the rest unallocated.
func TestBoardIDEveryValue(t *testing.T) {
	known := map[uint8]string{
		0x01: "atmega328p", 0x02: "atmega2560", 0x03: "atmega32u4",
		0x04: "attiny85", 0x40: "mps2-an505", 0x41: "frdm-imx95-m7", 0x42: "mps2-an500",
		0x43: "stm32h573i-dk", 0x44: "rp2350-pico2w",
		0x45: "nrf54l15-dk", 0x46: "frdm-mcxn947", 0x47: "nucleo-h563zi", 0x80: "esp32p4-poe", 0x81: "esp32-qemu",
	}
	seen := map[string]uint8{}
	for v := range 256 {
		id := BoardID(v)
		family := "unallocated"
		switch {
		case v >= 0x01 && v <= 0x3f:
			family = "avr"
		case v >= 0x40 && v <= 0x7f:
			family = "cortex-m"
		case v >= 0x80 && v <= 0xbf:
			family = "esp32/other"
		}
		if got := id.Family(); got != family {
			t.Errorf("board %#02x: family %q, want %q", v, got, family)
		}
		want, ok := known[uint8(v)]
		if !ok {
			want = fmt.Sprintf("unknown %s board 0x%02x", family, v)
		}
		got := id.String()
		if got != want {
			t.Errorf("board %#02x: %q, want %q", v, got, want)
		}
		if prev, dup := seen[got]; dup {
			t.Errorf("boards %#02x and %#02x both read %q", prev, v, got)
		}
		seen[got] = uint8(v)
	}
}
