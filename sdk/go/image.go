package tfw

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
)

// The OTA stream tools/image-seal writes (tfw_seal_stream): the image, an
// optional manifest envelope behind its two-byte little-endian length, 0xFF
// padding, and the image trailer (include/tfw/boot.h) in the last
// ImageTrailerLen bytes of a whole number of ImagePage pages.
const (
	// ImagePage is the stream's page, TFW_OTA_PAGE_MAX: a stream is a whole
	// number of them.
	ImagePage = 256
	// ImageTrailerLen is TFW_BOOT_TRAILER_LEN, the trailer at the stream's end.
	ImageTrailerLen = 128
	// ImageTrailerVersion is TFW_BOOT_TRAILER_VERSION, the one layout read.
	ImageTrailerVersion = 1
	// imageTrailerMagic is TFW_BOOT_TRAILER_MAGIC, 'T','F','I','M' little-endian.
	imageTrailerMagic = 0x4D494654
	// imageNoEnvelope is the length erased padding reads as.
	imageNoEnvelope = 0xFFFF
)

// The refusals of ParseImage, one per structural fault. Each comes wrapped
// with the numbers behind it: match with errors.Is.
var (
	// ErrImagePages says the stream is not a whole, non-zero number of pages.
	ErrImagePages = errors.New("tfw: image is not whole pages")
	// ErrImageMagic says the trailer does not begin with its magic.
	ErrImageMagic = errors.New("tfw: image trailer magic mismatch")
	// ErrImageVersion says the trailer is a layout other than version 1.
	ErrImageVersion = errors.New("tfw: image trailer version unknown")
	// ErrImageLength says image_len is zero or runs into the trailer.
	ErrImageLength = errors.New("tfw: image length out of range")
	// ErrImageBoard says the trailer names no board (board_id 0).
	ErrImageBoard = errors.New("tfw: image board_id is zero")
	// ErrImageEnvelope says the manifest envelope's length is zero or runs
	// past the padding into the trailer.
	ErrImageEnvelope = errors.New("tfw: image manifest envelope truncated")
)

// Image is an OTA stream whose structure ParseImage checked. Nothing here is
// authenticated: the signature, the measurement and the envelope are the
// device's to verify.
type Image struct {
	// Stream is the whole stream as given, what StageImage sends.
	Stream []byte
	// Board is the trailer's board_id, never zero.
	Board BoardID
	// Flags is the trailer's image_flags.
	Flags byte
	// Version is the trailer's image_version, the anti-rollback number.
	Version uint32
	// Len is the trailer's image_len, the bytes measured from the stream's start.
	Len uint32
	// Measurement is the trailer's SHA-256 over the first Len bytes.
	Measurement [32]byte
	// Envelope is the manifest envelope after the image, nil when none.
	Envelope []byte
}

// ParseImage checks an OTA stream's structure only: whole pages, the trailer's
// magic and version 1, its length, its board_id non-zero; and splits off an
// appended manifest envelope. It holds no key and verifies no signature.
func ParseImage(b []byte) (*Image, error) {
	if len(b) < ImagePage || len(b)%ImagePage != 0 {
		return nil, fmt.Errorf("%w: %d bytes, pages of %d", ErrImagePages, len(b), ImagePage)
	}
	tr := b[len(b)-ImageTrailerLen:]
	if m := binary.LittleEndian.Uint32(tr[0:4]); m != imageTrailerMagic {
		return nil, fmt.Errorf("%w: %#08x", ErrImageMagic, m)
	}
	if v := binary.LittleEndian.Uint16(tr[4:6]); v != ImageTrailerVersion {
		return nil, fmt.Errorf("%w: %d", ErrImageVersion, v)
	}
	rest := int64(len(b)) - ImageTrailerLen
	if rest < 0 || rest > math.MaxUint32 {
		return nil, fmt.Errorf("%w: %d bytes, pages of %d", ErrImagePages, len(b), ImagePage)
	}
	room := uint32(rest)
	n := binary.LittleEndian.Uint32(tr[12:16])
	if n == 0 || n > room {
		return nil, fmt.Errorf("%w: %d, room for 1 to %d", ErrImageLength, n, room)
	}
	if tr[6] == 0 {
		return nil, fmt.Errorf("%w", ErrImageBoard)
	}
	img := &Image{
		Stream:  b,
		Board:   BoardID(tr[6]),
		Flags:   tr[7],
		Version: binary.LittleEndian.Uint32(tr[8:12]),
		Len:     n,
	}
	copy(img.Measurement[:], tr[16:48])
	room -= n
	if room < 2 {
		return img, nil
	}
	el := uint32(binary.LittleEndian.Uint16(b[n : n+2]))
	if el == imageNoEnvelope {
		return img, nil
	}
	if el == 0 || el > room-2 {
		return nil, fmt.Errorf("%w: %d bytes, room for %d", ErrImageEnvelope, el, room-2)
	}
	img.Envelope = b[n+2 : n+2+el]
	return img, nil
}
