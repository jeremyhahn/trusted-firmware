// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"testing"

	"github.com/jeremyhahn/arduino-generic/sdk/go/internal/vectors"
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

// The discriminating case, stated as its own test because it is the one a
// future reimplementation is most likely to get wrong. A checksum that sums or
// XORs its bytes passes every case above and fails only this one.
func TestCRC16IsOrderSensitive(t *testing.T) {
	if CRC16([]byte{0x01, 0x02}) == CRC16([]byte{0x02, 0x01}) {
		t.Fatal("CRC16 gave the same value for two byte-permutations: the fold is order-blind")
	}
}

func TestCRC16IncrementalEqualsBulk(t *testing.T) {
	msg := []byte(vectors.CRCCheckInput)
	crc := crcInit
	for _, b := range msg {
		crc = CRC16Update(crc, b)
	}
	if crc != CRC16(msg) {
		t.Fatalf("incremental %#04x != bulk %#04x", crc, CRC16(msg))
	}
}
