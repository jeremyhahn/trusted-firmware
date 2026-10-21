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
 * tfw_crc16_update() has two definitions and exactly one is ever linked:
 *
 *   src/proto/crc16_update.c  -- portable C, table-free, one bit at a time.
 *     This is what host tests, tfwsim and the bootloader build against, and it
 *     is the definition the cross-implementation suite and the Go mirror
 *     check themselves against. One implementation compiled for host and
 *     device alike is one implementation that can disagree with itself in
 *     exactly zero ways, which is why this form is the one every non-firmware
 *     build keeps.
 *
 *   src/hal/avr/hal_crc16.c  -- avr-libc's _crc_xmodem_update, fourteen
 *     single-cycle instructions, branch-free. Linked ONLY into the firmware
 *     and the bench image (mk/avr.mk, mk/bench.mk's BENCH_SRCS), the same
 *     link-time substitution the rest of the HAL uses: the object list picks
 *     the definition, not a conditional inside this file.
 *
 * This used to be one function with a comment promising exactly this split
 * "if the cycle budget ever shows this mattering" -- #12 measured 102.5
 * cycles/byte against the fourteen-instruction alternative, which is the
 * measurement that comment was waiting for.
 *
 * Note the name of the avr-libc routine is misleading: that function is the
 * generic MSB-first 0x1021 update, and it is the INITIAL VALUE that picks the
 * variant -- 0x0000 yields XMODEM, 0xFFFF yields CCITT-FALSE, which is what
 * this is.
 */
#ifndef TFW_CRC16_H
#define TFW_CRC16_H

#include <stdint.h>

#define TFW_CRC16_INIT  0xFFFFu

/* The published check value: the CRC of the nine ASCII bytes "123456789".
 * Pinned in the test suite so a change to this file that alters the wire is a
 * test failure rather than a field incident. Both definitions of
 * tfw_crc16_update above must produce it. */
#define TFW_CRC16_CHECK 0x29B1u

/* Folds one byte into a running CRC. Exactly one of src/proto/crc16_update.c
 * and src/hal/avr/hal_crc16.c is linked in any given build; see the header
 * comment above for which. */
uint16_t tfw_crc16_update(uint16_t crc, uint8_t byte);

/* Convenience for a contiguous buffer; identical to seeding with
 * TFW_CRC16_INIT and folding each byte in turn. Portable in every build --
 * src/proto/crc16.c -- since it is a thin loop over whichever
 * tfw_crc16_update() is linked and is not itself the hot path. */
uint16_t tfw_crc16(const uint8_t *buf, uint16_t len);

/* The same loop carried on from a running CRC, for a CRC over two ranges that
 * are not contiguous: the handoff's crc16 covers what is before it and then
 * what is after it (include/tfw/boot.h, version 4). tfw_crc16 is this from
 * TFW_CRC16_INIT, so the two share one loop (#173). */
uint16_t tfw_crc16_from(uint16_t crc, const uint8_t *buf, uint16_t len);

#endif /* TFW_CRC16_H */
