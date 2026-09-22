/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * CRC-16/CCITT-FALSE  --  poly 0x1021, init 0xFFFF, no reflection, no final XOR.
 *
 * Chosen over CRC-32 because a table-driven CRC-32 costs 1 KB of PROGMEM, which
 * is an eighth of an ATtiny85's entire flash, and buys nothing at our frame
 * length. Chosen over CRC-8 because CRC-8 drops to Hamming distance 2 above
 * about fifteen bytes, and our frames reach 132; at that length this polynomial
 * still holds HD=4, so every one-, two- and three-bit error is detected, as is
 * every error of odd weight and every burst up to sixteen bits.
 *
 * Deliberately table-free and written in portable C rather than delegating to
 * avr-libc's _crc_xmodem_update on the target. Two reasons: the logic layers of
 * this codebase carry no target conditionals at all, and one implementation
 * compiled for both host and device is one implementation that can disagree
 * with itself in exactly zero ways. If the cycle budget in docs/development
 * ever shows this mattering, the measurement comes first and the specialisation
 * goes behind the HAL, not here.
 *
 * Note the name of the avr-libc routine is misleading: that function is the
 * generic MSB-first 0x1021 update, and it is the INITIAL VALUE that picks the
 * variant -- 0x0000 yields XMODEM, 0xFFFF yields CCITT-FALSE, which is what
 * this is.
 */
#ifndef AG_CRC16_H
#define AG_CRC16_H

#include <stdint.h>

#define AG_CRC16_INIT  0xFFFFu

/* The published check value: the CRC of the nine ASCII bytes "123456789".
 * Pinned in the test suite so a change to this file that alters the wire is a
 * test failure rather than a field incident. */
#define AG_CRC16_CHECK 0x29B1u

/* Folds one byte into a running CRC. */
uint16_t ag_crc16_update(uint16_t crc, uint8_t byte);

/* Convenience for a contiguous buffer; identical to seeding with
 * AG_CRC16_INIT and folding each byte in turn. */
uint16_t ag_crc16(const uint8_t *buf, uint16_t len);

#endif /* AG_CRC16_H */
