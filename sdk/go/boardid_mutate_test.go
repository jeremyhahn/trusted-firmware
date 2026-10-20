// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"fmt"
	"testing"
)

// TestBoardIDEveryValue: each of the 256 board ids names its own board or
// reads as unknown with its own value, never as another board; the family
// is the range the id sits in: 0x01-0x3F AVR, 0x40-0x7F Cortex-M, 0x80-0xBF
// ESP32 and other, the rest unallocated.
func TestBoardIDEveryValue(t *testing.T) {
	known := map[uint8]string{
		0x01: "atmega328p", 0x02: "atmega2560", 0x03: "atmega32u4",
		0x04: "attiny85", 0x80: "esp32p4-poe", 0x81: "esp32-qemu",
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
