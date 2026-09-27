/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "crc16.h"

/*
 * THE PORTABLE DEFINITION. Table-free, one bit at a time, and deliberately not
 * the fast path: this is what host tests, agsim, the bootloader and the
 * cross-implementation suite build against, so that the wire vectors and the
 * Go mirror are checked against the same logical function every build shares
 * -- one implementation across host and device is one implementation that
 * cannot disagree with itself. The AVR firmware and bench images link
 * src/hal/avr/hal_crc16.c instead; see crc16.h.
 *
 * The register is carried in `unsigned int` rather than `uint16_t` because
 * `crc << 1` integer-promotes its operand to signed `int` before shifting, and
 * the result then meets an unsigned polynomial constant in the XOR. That mixed
 * signedness is what -Wsign-conversion objects to, and it is worth objecting
 * to: the shift is a bit operation on a 16-bit register, and expressing it in a
 * signed type is a statement nobody means. Working in `unsigned int`, masking
 * back to sixteen bits each round, and narrowing exactly once on return keeps
 * every step unsigned and the intent visible.
 */
uint16_t ag_crc16_update(uint16_t crc, uint8_t byte)
{
    unsigned int reg = (unsigned int)crc ^ ((unsigned int)byte << 8);
    uint8_t      bit;

    for (bit = 0; bit < 8u; bit++) {
        if (reg & 0x8000u) {
            reg = ((reg << 1) ^ 0x1021u) & 0xFFFFu;
        } else {
            reg = (reg << 1) & 0xFFFFu;
        }
    }
    return (uint16_t)reg;
}
