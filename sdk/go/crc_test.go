// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"bytes"
	"encoding/binary"
	"math/bits"
	"math/rand/v2"
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// The Go and C implementations are pinned to the same published bytes. A
// failure here is a wire break, not a test to update.
func TestCRC16MatchesThePublishedVectors(t *testing.T) {
	for _, tc := range []struct {
		name string
		in   []byte
		want uint16
	}{
		{"check value", []byte(vectors.CRCCheckInput), vectors.CRCCheckResult},
		{"empty", []byte{}, vectors.CRCEmptyResult},
		{"one zero byte", []byte{0x00}, vectors.CRCZeroResult},
		{"two zero bytes", []byte{0x00, 0x00}, vectors.CRCZeroZero},
		{"all ones", []byte{0xFF}, vectors.CRCFFResult},
		{"order A", []byte{0x01, 0x02}, vectors.CRCOrderAResult},
		{"order B", []byte{0x02, 0x01}, vectors.CRCOrderBResult},
	} {
		if got := CRC16(tc.in); got != tc.want {
			t.Errorf("%s: CRC16 = %#04x, want %#04x", tc.name, got, tc.want)
		}
	}
}

// TestCRC16AgreesWithEveryFrameVector: the CRC each pinned frame carries,
// little-endian after the body, is CRC16 over its length byte through the end
// of its body; and the check vector holds.
func TestCRC16AgreesWithEveryFrameVector(t *testing.T) {
	if got := CRC16([]byte(vectors.CRCCheckInput)); got != vectors.CRCCheckResult {
		t.Fatalf("check vector: %#04x, want %#04x", got, vectors.CRCCheckResult)
	}
	for name, s := range frameVectors {
		b := []byte(s)
		claimed := binary.LittleEndian.Uint16(b[len(b)-2:])
		if got := CRC16(b[1 : len(b)-2]); got != claimed {
			t.Errorf("%s: CRC16 = %#04x, the frame carries %#04x", name, got, claimed)
		}
	}
}

// crcCodeword is a frame as the CRC sees it: the covered bytes (length byte
// through the end of the body) and the CRC after them, little-endian as the
// wire carries it.
type crcCodeword struct {
	name string
	cov  []byte
	crc  uint16
}

// crcCodewords are the longest pinned frame, OTA_INFO's reply, and a frame at
// the longest length the wire allows (LEN 67), its bytes drawn from the seed.
func crcCodewords(t *testing.T) []crcCodeword {
	t.Helper()
	frame := []byte(vectors.FrameOTAInfoResp)
	ota := crcCodeword{"OTA_INFO resp", frame[1 : len(frame)-2], binary.LittleEndian.Uint16(frame[len(frame)-2:])}
	rng := rand.New(rand.NewPCG(mutateSeed, 900))
	long := make([]byte, 1+maxLen)
	long[0] = maxLen
	for i := 1; i < len(long); i++ {
		long[i] = byte(rng.Uint32())
	}
	return []crcCodeword{ota, {"LEN 67", long, CRC16(long)}}
}

// detects reports whether the receiver catches the codeword with the bits at
// the given positions flipped: position i < 8*len(cov) is bit i%8 of covered
// byte i/8, the rest are bits of the stored CRC, low byte first.
func (c crcCodeword) detects(pos ...int) bool {
	cov, crc := bytes.Clone(c.cov), c.crc
	for _, i := range pos {
		if i < 8*len(cov) {
			cov[i/8] ^= 1 << (i % 8)
		} else {
			crc ^= 1 << (i - 8*len(cov))
		}
	}
	return CRC16(cov) != crc
}

// TestCRC16DetectsEveryOneAndTwoBitError: every one-bit and every two-bit
// error anywhere in the codeword, CRC included, is detected -- each worked
// directly, not through the syndromes below.
func TestCRC16DetectsEveryOneAndTwoBitError(t *testing.T) {
	for _, c := range crcCodewords(t) {
		n := 8*len(c.cov) + 16
		for i := range n {
			if !c.detects(i) {
				t.Fatalf("seed %#x %s: bit %d flipped is not detected", mutateSeed, c.name, i)
			}
			for j := i + 1; j < n; j++ {
				if !c.detects(i, j) {
					t.Fatalf("seed %#x %s: bits %d and %d flipped are not detected", mutateSeed, c.name, i, j)
				}
			}
		}
	}
}

// syndromes returns, for each bit position in the given order, what flipping
// it does to CRC16(cov) ^ crc: zero means undetected. The CRC is linear over
// GF(2) for a fixed length, so any error's syndrome is the XOR of its bits'
// -- TestCRC16SyndromesAreLinear checks that before the bursts rely on it.
func (c crcCodeword) syndromes(order func(i int) int) []uint16 {
	n := 8*len(c.cov) + 16
	base := CRC16(c.cov) ^ c.crc
	syn := make([]uint16, n)
	for i := range n {
		cov, crc := bytes.Clone(c.cov), c.crc
		p := order(i)
		if p < 8*len(cov) {
			cov[p/8] ^= 1 << (p % 8)
		} else {
			crc ^= 1 << (p - 8*len(cov))
		}
		syn[i] = CRC16(cov) ^ crc ^ base
	}
	return syn
}

// polyOrder numbers the codeword's bits as the CRC's polynomial reads them:
// each covered byte most significant bit first, then the CRC high byte first
// -- the order in which a 16-bit CRC promises to catch every burst of 16.
func polyOrder(covLen int) func(i int) int {
	return func(i int) int {
		if i < 8*covLen {
			return i/8*8 + 7 - i%8
		}
		j := i - 8*covLen
		return 8*covLen + (1-j/8)*8 + 7 - j%8
	}
}

// uartOrder numbers the bits as a UART sends them: byte by byte, least
// significant bit first, the CRC low byte first.
func uartOrder(i int) int { return i }

// undetectedBursts returns the start and length of every burst of 1 to 16
// bits, in the order the syndromes are numbered, that the CRC misses. The
// interior bits of each burst are walked in Gray-code order.
func undetectedBursts(syn []uint16) [][2]int {
	var miss [][2]int
	for l := 1; l <= 16; l++ {
		for s := 0; s+l <= len(syn); s++ {
			x := syn[s]
			if l == 1 {
				if x == 0 {
					miss = append(miss, [2]int{s, l})
				}
				continue
			}
			x ^= syn[s+l-1]
			for g := 0; ; g++ {
				if x == 0 {
					miss = append(miss, [2]int{s, l})
				}
				if g+1 >= 1<<(l-2) {
					break
				}
				x ^= syn[s+1+bits.TrailingZeros(uint(g+1))]
			}
		}
	}
	return miss
}

// TestCRC16SyndromesAreLinear: for random errors of 2 to 40 bits, the XOR of
// the single-bit syndromes predicts exactly what the receiver sees.
func TestCRC16SyndromesAreLinear(t *testing.T) {
	rng := rand.New(rand.NewPCG(mutateSeed, 901))
	for _, c := range crcCodewords(t) {
		syn := c.syndromes(uartOrder)
		for range 5000 {
			pos := make([]int, 2+rng.IntN(39))
			var x uint16
			seen := map[int]bool{}
			for k := range pos {
				for {
					pos[k] = rng.IntN(len(syn))
					if !seen[pos[k]] {
						break
					}
				}
				seen[pos[k]] = true
				x ^= syn[pos[k]]
			}
			if (x != 0) != c.detects(pos...) {
				t.Fatalf("seed %#x %s: bits %v: syndrome %#04x disagrees with the receiver", mutateSeed, c.name, pos, x)
			}
		}
	}
}

// TestCRC16DetectsEveryBurst: every burst of up to 16 bits in the
// polynomial's order is detected, CRC included.
//
// WIRE PROPERTY, not an SDK fault: on the serial line a byte goes out least
// significant bit first and the CRC low byte first, so a 16-bit burst on the
// line is not always a 16-bit burst to the polynomial. Every such burst
// inside the covered bytes is still caught; three that run from the last body
// byte into the CRC are not -- in OTA_INFO's reply, in line order, 15 bits at
// 379 and 16 bits at 371 and at 376 of 400. The frame layout is the C
// firmware's and is pinned; this test holds the line-order misses to bursts
// that reach the CRC.
func TestCRC16DetectsEveryBurst(t *testing.T) {
	for _, c := range crcCodewords(t) {
		if miss := undetectedBursts(c.syndromes(polyOrder(len(c.cov)))); len(miss) != 0 {
			t.Errorf("seed %#x %s: polynomial-order bursts (start, length) not detected: %v", mutateSeed, c.name, miss)
		}
		for _, m := range undetectedBursts(c.syndromes(uartOrder)) {
			if m[0]+m[1] <= 8*len(c.cov) {
				t.Errorf("seed %#x %s: line-order burst of %d bits at %d, inside the body, not detected", mutateSeed, c.name, m[1], m[0])
			}
		}
	}
}
