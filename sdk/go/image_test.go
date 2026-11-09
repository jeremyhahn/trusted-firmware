package tfw

import (
	"bytes"
	"errors"
	"testing"
)

// sealedStream lays out an OTA stream the way tools/image-seal writes one
// (tfw_seal_stream): the image, then an optional envelope behind its
// little-endian length, 0xFF padding, and the 128-byte trailer in the last
// bytes of a whole number of 256-byte pages. Every trailer field is a literal
// at its documented offset (include/tfw/boot.h), never a value ParseImage
// computes.
func sealedStream(pages, imageLen int, env []byte) []byte {
	b := bytes.Repeat([]byte{0xFF}, pages*256)
	for i := range imageLen {
		b[i] = byte(i)
	}
	if env != nil {
		b[imageLen] = byte(len(env))
		b[imageLen+1] = byte(len(env) >> 8)
		copy(b[imageLen+2:], env)
	}
	t := b[len(b)-128:]
	copy(t, []byte{
		'T', 'F', 'I', 'M', // magic
		0x01, 0x00, // trailer_version 1
		0x02,                   // board_id
		0x01,                   // image_flags: development
		0x07, 0x00, 0x00, 0x00, // image_version 7
		byte(imageLen), byte(imageLen >> 8), byte(imageLen >> 16), byte(imageLen >> 24),
	})
	for i := 16; i < 48; i++ {
		t[i] = 0xA5 // measurement
	}
	for i := 48; i < 112; i++ {
		t[i] = 0x5A // signature
	}
	for i := 112; i < 128; i++ {
		t[i] = 0x00 // reserved
	}
	return b
}

// envelope is a stand-in for a SUIT envelope: ParseImage splits it off by its
// length and never reads it.
var envelope = []byte{0xD8, 0x6B, 0xA2, 0x02, 0x58, 0x01, 0x00, 0x03, 0x41, 0x00}

func TestParseImageSplitsAGoodStream(t *testing.T) {
	cases := []struct {
		name     string
		stream   []byte
		imageLen uint32
		env      []byte
	}{
		{"no envelope", sealedStream(2, 300, nil), 300, nil},
		{"envelope", sealedStream(2, 300, envelope), 300, envelope},
		{"image fills to the trailer", sealedStream(1, 128, nil), 128, nil},
		{"one byte before the trailer", sealedStream(1, 127, nil), 127, nil},
		{"envelope fills to the trailer", sealedStream(1, 128-2-len(envelope), envelope),
			uint32(128 - 2 - len(envelope)), envelope},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			img, err := ParseImage(c.stream)
			if err != nil {
				t.Fatalf("ParseImage: %v", err)
			}
			if !bytes.Equal(img.Stream, c.stream) {
				t.Error("Stream is not the bytes given")
			}
			if img.Board != BoardID(0x02) || img.Flags != 0x01 || img.Version != 7 {
				t.Errorf("board %d flags %#x version %d, want 2 0x01 7",
					img.Board, img.Flags, img.Version)
			}
			if img.Len != c.imageLen {
				t.Errorf("Len %d, want %d", img.Len, c.imageLen)
			}
			if img.Measurement != [32]byte(bytes.Repeat([]byte{0xA5}, 32)) {
				t.Error("Measurement is not trailer bytes [16, 48)")
			}
			if !bytes.Equal(img.Envelope, c.env) || (c.env == nil) != (img.Envelope == nil) {
				t.Errorf("Envelope % x, want % x", img.Envelope, c.env)
			}
		})
	}
}

func TestParseImageRefusesEachStructuralFault(t *testing.T) {
	good := func() []byte { return sealedStream(2, 300, envelope) }
	trailer := func(b []byte) []byte { return b[len(b)-128:] }
	cases := []struct {
		name   string
		stream func() []byte
		want   error
	}{
		{"empty", func() []byte { return nil }, ErrImagePages},
		{"shorter than the trailer", func() []byte { return good()[:127] }, ErrImagePages},
		{"a byte short of a page", func() []byte { b := good(); return b[:len(b)-1] }, ErrImagePages},
		{"a byte past a page", func() []byte { return append(good(), 0xFF) }, ErrImagePages},
		{"bad magic", func() []byte { b := good(); trailer(b)[3] = 'X'; return b }, ErrImageMagic},
		{"version 0", func() []byte { b := good(); trailer(b)[4] = 0x00; return b }, ErrImageVersion},
		{"version 2", func() []byte { b := good(); trailer(b)[4] = 0x02; return b }, ErrImageVersion},
		{"version 0x0101", func() []byte { b := good(); trailer(b)[5] = 0x01; return b }, ErrImageVersion},
		{"length zero", func() []byte {
			b := good()
			copy(trailer(b)[12:16], []byte{0x00, 0x00, 0x00, 0x00})
			return b
		}, ErrImageLength},
		{"length into the trailer", func() []byte {
			b := good()
			copy(trailer(b)[12:16], []byte{0x81, 0x01, 0x00, 0x00}) // 385 of 384
			return b
		}, ErrImageLength},
		{"length past the stream", func() []byte {
			b := good()
			copy(trailer(b)[12:16], []byte{0x00, 0x00, 0x00, 0x80})
			return b
		}, ErrImageLength},
		{"zero board_id", func() []byte { b := good(); trailer(b)[6] = 0x00; return b }, ErrImageBoard},
		{"envelope length zero", func() []byte {
			b := good()
			b[300], b[301] = 0x00, 0x00
			return b
		}, ErrImageEnvelope},
		{"envelope past the padding", func() []byte {
			b := good()
			b[300], b[301] = 83, 0x00 // 84 bytes of room hold 82 after the length
			return b
		}, ErrImageEnvelope},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			img, err := ParseImage(c.stream())
			if !errors.Is(err, c.want) {
				t.Fatalf("err %v, want %v", err, c.want)
			}
			if img != nil {
				t.Error("an Image came back with the error")
			}
			for _, other := range []error{ErrImagePages, ErrImageMagic, ErrImageVersion,
				ErrImageLength, ErrImageBoard, ErrImageEnvelope} {
				if other != c.want && errors.Is(err, other) {
					t.Errorf("err %v also reads as %v", err, other)
				}
			}
		})
	}
}
