// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
//
// The exact algorithm src/proto/crc16.c implements, deliberately written the
// same way rather than table-driven. The two are pinned to the same published
// vectors, and a table here would be a second thing to get right for a gain
// nobody on this side of the link can measure.
const crcInit uint16 = 0xFFFF

// CRC16Update folds one byte into a running CRC.
func CRC16Update(crc uint16, b byte) uint16 {
	crc ^= uint16(b) << 8
	for i := 0; i < 8; i++ {
		if crc&0x8000 != 0 {
			crc = (crc << 1) ^ 0x1021
		} else {
			crc <<= 1
		}
	}
	return crc
}

// CRC16 computes the CRC of a buffer.
func CRC16(b []byte) uint16 {
	crc := crcInit
	for _, x := range b {
		crc = CRC16Update(crc, x)
	}
	return crc
}
