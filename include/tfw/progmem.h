/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one toolchain shim in the tree.
 *
 * AVR puts constant tables in flash and needs an explicit instruction to read
 * them; every other target just dereferences a pointer. That difference is a
 * property of the compiler, not of this firmware, so it is isolated here and
 * the board tables below are written once for both.
 *
 * This file and the src/hal/<arch>/ and boot/hal/<arch>/ directories are the
 * only places a target conditional appears outside config.h, a board profile
 * and commands.def; `make lint` enforces it. A shim that hides a
 * toolchain difference is not the thing the no-#if rule exists to prevent;
 * logic that behaves differently per target is, and there is none of that here.
 */
#ifndef TFW_PROGMEM_H
#define TFW_PROGMEM_H

#include <stdint.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#define TFW_PROGMEM        PROGMEM
#define tfw_pgm_u8(p)      pgm_read_byte(p)
#define tfw_pgm_u16(p)     pgm_read_word(p)
#define tfw_pgm_u32(p)     ((uint32_t)pgm_read_dword(p))
#define tfw_pgm_copy(d, s, n) memcpy_P((d), (s), (n))
#else
#include <string.h>
#define TFW_PROGMEM        /* nothing: the host has one address space */
#define tfw_pgm_u8(p)      (*(const uint8_t *)(p))
#define tfw_pgm_u16(p)     (*(const uint16_t *)(p))
#define tfw_pgm_u32(p)     (*(const uint32_t *)(p))
#define tfw_pgm_copy(d, s, n) memcpy((d), (s), (n))
#endif

#endif /* TFW_PROGMEM_H */
