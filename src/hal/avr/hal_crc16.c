/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <util/crc16.h>

#include "proto/crc16.h"

/*
 * THE FAST DEFINITION, linked ONLY into the firmware and bench images --
 * mk/avr.mk's AVR_SRCS globs every .c in this directory, and mk/bench.mk's
 * BENCH_SRCS is that same list with main.c exchanged, so this file reaches
 * both without either Makefile naming it. Every other build (host, agsim, the
 * bootloader, the cross-implementation suite) links
 * src/proto/crc16_update.c instead; see that file's header and crc16.h for
 * why there are two.
 *
 * avr-libc's _crc_xmodem_update computes the same polynomial as the portable
 * form with the same output -- fourteen single-cycle instructions, branch-
 * free, folding a nibble at a time with swap/andi/eor -- against the
 * portable loop's eight conditional shifts a byte. #12 measured the
 * difference at 102.5 cycles/byte (328P) and 104.5 (2560) versus this.
 *
 * The name is misleading: the function is the generic MSB-first 0x1021
 * update, and it is the INITIAL VALUE folded in by the caller that picks the
 * variant -- 0x0000 yields XMODEM, 0xFFFF yields CCITT-FALSE, which is what
 * AG_CRC16_INIT is and what this firmware speaks. Nothing about the polynomial
 * or the output changes; only the instruction count does, which is exactly
 * what a HAL substitution is for.
 */
uint16_t ag_crc16_update(uint16_t crc, uint8_t byte)
{
    return _crc_xmodem_update(crc, byte);
}
