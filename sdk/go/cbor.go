// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"encoding/binary"
	"fmt"
	"unicode/utf8"
)

// The CBOR this module reads is the claims set the device writes
// (docs/dice-attestation/api.md): core-deterministic, definite lengths,
// unsigned integers, byte and text strings, arrays and maps keyed by unsigned
// integers. Anything else is refused rather than skipped, because a decoder
// that tolerated what the device never writes would accept a claims set the
// device never wrote. It is not a general CBOR library and is not exported:
// the SDK reads one shape, and go-xkms owns everything a verifier does with it.

// cborMaxDepth bounds nesting. The deepest item the device writes is the
// hash-entry, five levels below the claims set.
const cborMaxDepth = 8

type cborReader struct {
	b []byte
}

// cborDecode decodes exactly one item from b, refusing trailing bytes.
func cborDecode(b []byte) (any, error) {
	r := cborReader{b: b}
	v, err := r.item(0)
	if err != nil {
		return nil, err
	}
	if len(r.b) != 0 {
		return nil, fmt.Errorf("%d trailing bytes after the item", len(r.b))
	}
	return v, nil
}

// head reads an item's initial byte and argument.
func (r *cborReader) head() (major byte, arg uint64, err error) {
	if len(r.b) == 0 {
		return 0, 0, fmt.Errorf("truncated")
	}
	ib := r.b[0]
	r.b = r.b[1:]
	major, info := ib>>5, ib&0x1f
	var n int
	switch {
	case info < 24:
		return major, uint64(info), nil
	case info == 24:
		n = 1
	case info == 25:
		n = 2
	case info == 26:
		n = 4
	case info == 27:
		n = 8
	default:
		return 0, 0, fmt.Errorf("additional information %d is not a definite length", info)
	}
	if len(r.b) < n {
		return 0, 0, fmt.Errorf("truncated")
	}
	var buf [8]byte
	copy(buf[8-n:], r.b[:n])
	r.b = r.b[n:]
	return major, binary.BigEndian.Uint64(buf[:]), nil
}

// count turns an argument into a length no larger than the bytes left, each
// element taking at least one, so a forged length cannot ask for memory the
// input could never fill.
func (r *cborReader) count(arg uint64) (int, error) {
	if arg > uint64(len(r.b)) {
		return 0, fmt.Errorf("length %d exceeds the %d bytes left", arg, len(r.b))
	}
	return int(arg), nil // #nosec G115 -- arg is at most len(r.b): the check above bounds it
}

func (r *cborReader) item(depth int) (any, error) {
	if depth > cborMaxDepth {
		return nil, fmt.Errorf("nested deeper than %d", cborMaxDepth)
	}
	major, arg, err := r.head()
	if err != nil {
		return nil, err
	}
	switch major {
	case 0:
		return arg, nil
	case 2, 3:
		n, err := r.count(arg)
		if err != nil {
			return nil, err
		}
		s := r.b[:n:n]
		r.b = r.b[n:]
		if major == 3 {
			if !utf8.Valid(s) {
				return nil, fmt.Errorf("text string is not UTF-8")
			}
			return string(s), nil
		}
		return s, nil
	case 4:
		n, err := r.count(arg)
		if err != nil {
			return nil, err
		}
		a := make([]any, 0, n)
		for range n {
			v, err := r.item(depth + 1)
			if err != nil {
				return nil, err
			}
			a = append(a, v)
		}
		return a, nil
	case 5:
		n, err := r.count(arg)
		if err != nil {
			return nil, err
		}
		m := make(map[uint64]any, n)
		for range n {
			k, err := r.item(depth + 1)
			if err != nil {
				return nil, err
			}
			key, ok := k.(uint64)
			if !ok {
				return nil, fmt.Errorf("map key %v is not an unsigned integer", k)
			}
			if _, dup := m[key]; dup {
				return nil, fmt.Errorf("map key %d appears twice", key)
			}
			v, err := r.item(depth + 1)
			if err != nil {
				return nil, err
			}
			m[key] = v
		}
		return m, nil
	default:
		return nil, fmt.Errorf("major type %d is not one the device writes", major)
	}
}
