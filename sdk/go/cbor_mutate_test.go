// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"errors"
	"fmt"
	"math/bits"
	"math/rand/v2"
	"reflect"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// mutateSeed is the seed of every random mutation in the _mutate_test.go
// files; each failure message names it so a run can be repeated exactly.
const mutateSeed = 0x163

// mutation is one changed copy of a valid reply. lo and hi bound the bytes
// it changed in the original's offsets: a truncation changed [len, len) by
// removing the tail, an append changed [len, len) by adding one.
type mutation struct {
	kind   string
	lo, hi int
	b      []byte
}

func (m mutation) String() string {
	return fmt.Sprintf("seed %#x %s at [%d,%d): % x", mutateSeed, m.kind, m.lo, m.hi, m.b)
}

// eachMutation calls fn with every single-byte change of valid (which
// includes every one-bit flip, reported as kind "flip"), every truncation,
// valid with bytes appended, and a fixed count of seeded multi-byte changes.
// stream separates the random draws of one caller from another's.
func eachMutation(valid []byte, stream uint64, fn func(m mutation)) {
	for off := range valid {
		for x := range 256 {
			if byte(x) == valid[off] {
				continue
			}
			b := bytes.Clone(valid)
			b[off] = byte(x)
			kind := "byte"
			if bits.OnesCount8(b[off]^valid[off]) == 1 {
				kind = "flip"
			}
			fn(mutation{kind: kind, lo: off, hi: off + 1, b: b})
		}
	}
	for n := range len(valid) {
		fn(mutation{kind: "truncate", lo: n, hi: len(valid), b: bytes.Clone(valid[:n])})
	}
	rng := rand.New(rand.NewPCG(mutateSeed, stream))
	for _, tail := range [][]byte{{0x00}, {0xff}, {0x00, 0x00}} {
		fn(mutation{kind: "append", lo: len(valid), hi: len(valid), b: append(bytes.Clone(valid), tail...)})
	}
	for range 64 {
		tail := make([]byte, 1+rng.IntN(8))
		for i := range tail {
			tail[i] = byte(rng.Uint32())
		}
		fn(mutation{kind: "append", lo: len(valid), hi: len(valid), b: append(bytes.Clone(valid), tail...)})
	}
	if len(valid) < 2 {
		return
	}
	for range 2000 {
		b := bytes.Clone(valid)
		lo, hi := len(b), 0
		for range 2 + rng.IntN(3) {
			off := rng.IntN(len(b))
			b[off] = byte(rng.Uint32())
			lo, hi = min(lo, off), max(hi, off+1)
		}
		if bytes.Equal(b, valid) {
			continue
		}
		fn(mutation{kind: "random", lo: lo, hi: hi, b: b})
	}
}

// noPanic runs f and turns a panic into a failure naming the mutation.
func noPanic(t *testing.T, m mutation, f func()) {
	t.Helper()
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("%v: panic: %v", m, r)
		}
	}()
	f()
}

// appEvHashOff is where the 32-byte hash-value sits in both claims-set
// vectors (appevidence_test.go reads AppEvWithoutUccs[111:143]).
const appEvHashOff = 111

// appEvCoSWID returns the CoSWID byte string inside a claims-set vector: the
// 84 bytes after its 0x58 0x54 head, located in the literal.
func appEvCoSWID(t *testing.T, v []byte) []byte {
	t.Helper()
	i := bytes.Index(v, []byte{0x58, 0x54, 0xa5})
	if i < 0 || i+2+0x54 > len(v) {
		t.Fatalf("no CoSWID byte string in the vector")
	}
	return v[i+2 : i+2+0x54]
}

// TestCBORDecodeMutations: every structure the SDK decodes -- the UCCS claims
// set, with and without the application's claim, and the CoSWID inside it --
// answers every mutation with an error or a value, never a panic; no
// truncation or append decodes; and no single-byte change decodes to the
// original's value.
func TestCBORDecodeMutations(t *testing.T) {
	with := []byte(vectors.AppEvWithUccs)
	without := []byte(vectors.AppEvWithoutUccs)
	for i, valid := range [][]byte{with, without, appEvCoSWID(t, with), appEvCoSWID(t, without)} {
		orig, err := cborDecode(valid)
		if err != nil {
			t.Fatalf("vector %d does not decode: %v", i, err)
		}
		eachMutation(valid, uint64(i), func(m mutation) {
			var v any
			var err error
			noPanic(t, m, func() { v, err = cborDecode(m.b) })
			if err != nil {
				return
			}
			switch m.kind {
			case "truncate", "append":
				t.Fatalf("vector %d %v: decoded to %v", i, m, v)
			case "byte", "flip":
				if reflect.DeepEqual(v, orig) {
					t.Fatalf("vector %d %v: decoded to the original's value", i, m)
				}
			}
		})
	}
}

// TestApplicationEvidenceMutations: a mutated claims set answers an
// *EvidenceError or 32 bytes. Only the hash-value is read (ApplicationEvidence
// returns that and nothing else), so a change inside it that parses answers
// the changed bytes, and a change anywhere else that parses answers the
// original's.
func TestApplicationEvidenceMutations(t *testing.T) {
	with := []byte(vectors.AppEvWithUccs)
	without := []byte(vectors.AppEvWithoutUccs)
	if !bytes.Equal(with[appEvHashOff:appEvHashOff+ApplicationEvidenceLen], []byte(vectors.AppEvExample)) {
		t.Fatalf("AppEvWithUccs does not carry AppEvExample at %d", appEvHashOff)
	}
	for i, valid := range [][]byte{with, without} {
		orig := valid[appEvHashOff : appEvHashOff+ApplicationEvidenceLen]
		eachMutation(valid, 10+uint64(i), func(m mutation) {
			var ev [ApplicationEvidenceLen]byte
			var err error
			noPanic(t, m, func() { ev, err = ApplicationEvidence(m.b) })
			if err != nil {
				var e *EvidenceError
				if !errors.As(err, &e) {
					t.Fatalf("vector %d %v: error %v is not an *EvidenceError", i, m, err)
				}
				return
			}
			if m.kind == "truncate" || m.kind == "append" {
				t.Fatalf("vector %d %v: parsed to % x", i, m, ev)
			}
			// Same length as the original, so the hash-value is where it was:
			// the answer is those bytes, changed or not, whatever else moved.
			want := m.b[appEvHashOff : appEvHashOff+ApplicationEvidenceLen]
			if m.hi <= appEvHashOff || m.lo >= appEvHashOff+ApplicationEvidenceLen {
				if !bytes.Equal(want, orig) {
					t.Fatalf("vector %d %v: harness changed the hash-value", i, m)
				}
			}
			if !bytes.Equal(ev[:], want) {
				t.Fatalf("vector %d %v: answered % x, want % x", i, m, ev, want)
			}
		})
	}
}

// nest wraps leaf in n one-entry arrays (0x81) or maps (0xa1 0x00).
func nest(n int, mapped bool, leaf byte) []byte {
	var b []byte
	for range n {
		if mapped {
			b = append(b, 0xa1, 0x00)
		} else {
			b = append(b, 0x81)
		}
	}
	return append(b, leaf)
}

// TestCBORLimits: nesting depth and length fields at their limits decode,
// and one past them is refused.
func TestCBORLimits(t *testing.T) {
	ok := map[string][]byte{
		"arrays at the depth limit":   nest(cborMaxDepth, false, 0x00),
		"maps at the depth limit":     nest(cborMaxDepth, true, 0x00),
		"bytes filling the input":     {0x43, 1, 2, 3},
		"bytes, 1-byte length":        append([]byte{0x58, 24}, make([]byte, 24)...),
		"bytes, 2-byte length":        append([]byte{0x59, 0x01, 0x00}, make([]byte, 256)...),
		"bytes, 4-byte length":        {0x5a, 0, 0, 0, 3, 1, 2, 3},
		"bytes, 8-byte length":        {0x5b, 0, 0, 0, 0, 0, 0, 0, 3, 1, 2, 3},
		"text filling the input":      {0x63, 'a', 'p', 'p'},
		"array count equal to items":  {0x83, 0, 0, 0},
		"map count equal to pairs":    {0xa2, 0, 0, 1, 0},
		"largest unsigned":            {0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
		"empty bytes, empty array":    {0x82, 0x40, 0x80},
		"empty map":                   {0xa0},
		"zero-length text in a map":   {0xa1, 0x18, 0x18, 0x60},
		"24-entry array, 1-byte head": append([]byte{0x98, 24}, make([]byte, 24)...),
	}
	for name, b := range ok {
		if _, err := cborDecode(b); err != nil {
			t.Errorf("%s (% x): %v", name, b, err)
		}
	}
	bad := map[string][]byte{
		"arrays past the depth limit":  nest(cborMaxDepth+1, false, 0x00),
		"maps past the depth limit":    nest(cborMaxDepth+1, true, 0x00),
		"bytes one past the input":     {0x44, 1, 2, 3},
		"bytes, 1-byte length past":    append([]byte{0x58, 25}, make([]byte, 24)...),
		"bytes, 2-byte length past":    append([]byte{0x59, 0x01, 0x01}, make([]byte, 256)...),
		"bytes, 4-byte length past":    {0x5a, 0, 0, 0, 4, 1, 2, 3},
		"bytes, 8-byte length maximum": {0x5b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0},
		"bytes, 8-byte length 2^32":    {0x5b, 0, 0, 0, 1, 0, 0, 0, 0, 0},
		"text one past the input":      {0x64, 'a', 'p', 'p'},
		"text not UTF-8":               {0x62, 0xc3, 0x28},
		"array count past items":       {0x84, 0, 0, 0},
		"array, 8-byte count maximum":  {0x9b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0},
		"map count past pairs":         {0xa3, 0, 0, 1, 0},
		"map, 8-byte count maximum":    {0xbb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0},
		"map key twice":                {0xa2, 0, 0, 0, 1},
		"map key a text":               {0xa1, 0x61, 'a', 0},
		"unsigned head truncated":      {0x1b, 0, 0, 0},
		"empty input":                  {},
		"negative integer":             {0x20},
		"tag":                          {0xc0, 0x00},
		"simple value":                 {0xf5},
		"float":                        {0xf9, 0x3c, 0x00},
		"indefinite bytes":             {0x5f, 0x41, 0x00, 0xff},
		"indefinite array":             {0x9f, 0x00, 0xff},
		"indefinite map":               {0xbf, 0x00, 0x00, 0xff},
		"reserved info 28":             {0x1c},
		"reserved info 29":             {0x3d},
		"reserved info 30":             {0x9e},
		"trailing byte":                {0x00, 0x00},
	}
	for name, b := range bad {
		if v, err := cborDecode(b); err == nil {
			t.Errorf("%s (% x): decoded to %v", name, b, v)
		}
	}
}
