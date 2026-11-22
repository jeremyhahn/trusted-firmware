// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"testing"

	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/vectors"
)

// TestCRC16UpdateHonoursItsSeed: CRC16Update folds from whatever CRC it is
// handed, not only 0xFFFF. The same polynomial from other seeds has published
// check values over "123456789": 0x0000 is CRC-16/XMODEM, 0x1D0F is
// CRC-16/SPI-FUJITSU (AUG-CCITT).
func TestCRC16UpdateHonoursItsSeed(t *testing.T) {
	for _, tc := range []struct {
		seed, want uint16
	}{
		{0x0000, 0x31C3},
		{0x1D0F, 0xE5CC},
		{0xFFFF, vectors.CRCCheckResult},
	} {
		crc := tc.seed
		for _, b := range []byte(vectors.CRCCheckInput) {
			crc = CRC16Update(crc, b)
		}
		if crc != tc.want {
			t.Errorf("seed %#04x: CRC16Update over the check input = %#04x, want %#04x", tc.seed, crc, tc.want)
		}
	}
	if got := CRC16Update(0x0000, 0x01); got != 0x1021 {
		t.Errorf("CRC16Update(0x0000, 0x01) = %#04x, want the polynomial 0x1021", got)
	}
}

// TestCRC16UpdateResumesARunningCRC: folding a buffer's tail byte by byte
// onto CRC16 of its head gives CRC16 of the whole, at every split of the
// check input and at the middle of every frame vector's covered bytes.
func TestCRC16UpdateResumesARunningCRC(t *testing.T) {
	resume := func(b []byte, k int) uint16 {
		crc := CRC16(b[:k])
		for _, x := range b[k:] {
			crc = CRC16Update(crc, x)
		}
		return crc
	}
	b := []byte(vectors.CRCCheckInput)
	for k := 0; k <= len(b); k++ {
		if got, want := resume(b, k), CRC16(b); got != want {
			t.Errorf("check input split at %d: resumed CRC = %#04x, want %#04x", k, got, want)
		}
	}
	for name, s := range frameVectors {
		cov := []byte(s)[1 : len(s)-2]
		if got, want := resume(cov, len(cov)/2), CRC16(cov); got != want {
			t.Errorf("%s split at %d: resumed CRC = %#04x, want %#04x", name, len(cov)/2, got, want)
		}
	}
}
