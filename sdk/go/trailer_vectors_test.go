// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"encoding/binary"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// The image trailer v1 signed spans testdata/vectors.h froze (#76). The SDK
// has no trailer parser yet; this holds the mirrored spans to the layout in
// include/tfw/boot.h and to the board-id registry the SDK carries, so a span
// that names the wrong board, or a registry id that moves, fails here.
func TestTrailerVectorsBindTheRegistryBoards(t *testing.T) {
	cases := []struct {
		name  string
		span  string
		board BoardID
		flags byte
	}{
		{"atmega328p", vectors.TrailerATmega328PSpan, BoardATmega328P, 0},
		{"atmega2560", vectors.TrailerATmega2560Span, BoardATmega2560, 0},
		{"atmega32u4", vectors.TrailerATmega32U4Span, BoardATmega32U4, 0},
		{"attiny85", vectors.TrailerATtiny85Span, BoardATtiny85, 0},
		{"esp32p4-poe", vectors.TrailerESP32P4PoESpan, BoardESP32P4PoE, 0},
		{"esp32-qemu", vectors.TrailerESP32QEMUSpan, BoardESP32QEMU, 0},
		{"mismatch", vectors.TrailerMismatchSpan, BoardATmega2560, 0},
		{"board zero", vectors.TrailerBoardZeroSpan, BoardID(0), 0},
		{"reserved flag", vectors.TrailerReservedFlagSpan, BoardATmega328P, 0x02},
		{"dev image", vectors.TrailerDevImageSpan, BoardATmega328P, 0x01},
	}
	for _, c := range cases {
		b := []byte(c.span)
		if len(b) != vectors.TrailerSpanLen {
			t.Fatalf("%s: span is %d bytes, want %d", c.name, len(b), vectors.TrailerSpanLen)
		}
		if got := binary.LittleEndian.Uint32(b[0:4]); got != 0x4D494654 {
			t.Errorf("%s: magic %#08x, want 0x4d494654", c.name, got)
		}
		if got := binary.LittleEndian.Uint16(b[4:6]); got != 1 {
			t.Errorf("%s: trailer_version %d, want 1", c.name, got)
		}
		if got := BoardID(b[6]); got != c.board {
			t.Errorf("%s: board_id %#02x, want %#02x", c.name, byte(got), byte(c.board))
		}
		if b[7] != c.flags {
			t.Errorf("%s: image_flags %#02x, want %#02x", c.name, b[7], c.flags)
		}
	}
}
