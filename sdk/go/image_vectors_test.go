// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"errors"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// streamAroundSpan builds a 17-page stream (4352 bytes) around a frozen
// trailer span: a 4096-byte image of a non-0xFF pattern, 0xFF after it (so
// the length bytes at [4096, 4098) read 0xFFFF and no envelope follows), the
// span in the trailer's first 48 bytes and the rest of the trailer 0xFF.
func streamAroundSpan(span string) []byte {
	b := bytes.Repeat([]byte{0xff}, 17*ImagePage)
	for i := 0; i < 0x1000; i++ {
		b[i] = byte(i % 251)
	}
	copy(b[len(b)-ImageTrailerLen:], span)
	return b
}

// ParseImage reads the trailer spans testdata/vectors.h froze, so its offsets
// are held to bytes the C side produced, not to a second hand-written layout.
// ParseImage checks structure only: the mismatch (a 2560 image a 328P
// refuses) and the reserved flag, which the device refuses, parse here.
func TestParseImageReadsTheFrozenTrailerSpans(t *testing.T) {
	cases := []struct {
		name  string
		span  string
		board BoardID
		flags uint8
	}{
		{"atmega328p", vectors.TrailerATmega328PSpan, BoardATmega328P, 0},
		{"atmega2560", vectors.TrailerATmega2560Span, BoardATmega2560, 0},
		{"atmega32u4", vectors.TrailerATmega32U4Span, BoardATmega32U4, 0},
		{"attiny85", vectors.TrailerATtiny85Span, BoardATtiny85, 0},
		{"esp32p4-poe", vectors.TrailerESP32P4PoESpan, BoardESP32P4PoE, 0},
		{"esp32-qemu", vectors.TrailerESP32QEMUSpan, BoardESP32QEMU, 0},
		{"mismatch", vectors.TrailerMismatchSpan, BoardATmega2560, 0},
		{"reserved-flag", vectors.TrailerReservedFlagSpan, BoardATmega328P, 0x02},
		{"dev-image", vectors.TrailerDevImageSpan, BoardATmega328P, 0x01},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if len(c.span) != vectors.TrailerSpanLen {
				t.Fatalf("span is %d bytes, want %d", len(c.span), vectors.TrailerSpanLen)
			}
			stream := streamAroundSpan(c.span)
			img, err := ParseImage(stream)
			if err != nil {
				t.Fatalf("ParseImage: %v", err)
			}
			if img.Board != c.board {
				t.Errorf("Board = %#02x, want %#02x", img.Board, c.board)
			}
			if img.Flags != c.flags {
				t.Errorf("Flags = %#02x, want %#02x", img.Flags, c.flags)
			}
			if img.Version != 3 {
				t.Errorf("Version = %d, want 3", img.Version)
			}
			if img.Len != 0x1000 {
				t.Errorf("Len = %#x, want 0x1000", img.Len)
			}
			if !bytes.Equal(img.Measurement[:], []byte(c.span[16:48])) {
				t.Errorf("Measurement = %x, want %x", img.Measurement, c.span[16:48])
			}
			if img.Envelope != nil {
				t.Errorf("Envelope = %x, want nil", img.Envelope)
			}
			if !bytes.Equal(img.Stream, stream) {
				t.Error("Stream is not the bytes given")
			}
		})
	}
}

// The frozen span that names no board is refused.
func TestParseImageRefusesTheFrozenBoardZeroSpan(t *testing.T) {
	img, err := ParseImage(streamAroundSpan(vectors.TrailerBoardZeroSpan))
	if !errors.Is(err, ErrImageBoard) {
		t.Fatalf("ParseImage err = %v, want ErrImageBoard", err)
	}
	if img != nil {
		t.Errorf("ParseImage image = %+v, want nil", img)
	}
}
