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
/* A constant the boot holds, read where the boot sits. Above 64 KB (the 2560's
 * boot section is at 0x3e000) LPM reads a 16-bit address, so the plain reads
 * above alias into the application. These take the table's symbol as `base`,
 * load RAMPZ with its 64 KB page and read `p`'s low 16 bits with ELPM: two
 * instructions more than LPM, where pgm_get_far_address with
 * pgm_read_byte_far would carry a 32-bit address through every index. They
 * hold for a table that does not straddle a 64 KB line, which nothing in an
 * 8 KB boot section can. RAMPZ is left set, as pgm_read_byte_far leaves it.
 * At or below 64 KB they are the plain reads, byte for byte. */
#if FLASHEND > 0xFFFFUL
#define tfw_pgm_far_u8(base, p) (__extension__({                           \
    uint8_t v_;                                                             \
    __asm__("ldi %0, hh8(%2)" "\n\t"                                        \
            "out %3, %0"      "\n\t"                                        \
            "elpm %0, Z"                                                    \
            : "=&d"(v_) : "z"((uint16_t)(p)), "p"(&(base)),                 \
              "I"(_SFR_IO_ADDR(RAMPZ)));                                    \
    v_; }))
#define tfw_pgm_far_u32(base, p) (__extension__({                          \
    uint32_t v_;                                                            \
    uint16_t z_ = (uint16_t)(p);                                            \
    __asm__("ldi %A0, hh8(%2)" "\n\t"                                       \
            "out %3, %A0"      "\n\t"                                       \
            "elpm %A0, Z+"     "\n\t"                                       \
            "elpm %B0, Z+"     "\n\t"                                       \
            "elpm %C0, Z+"     "\n\t"                                       \
            "elpm %D0, Z"                                                   \
            : "=&d"(v_), "+z"(z_) : "p"(&(base)),                           \
              "I"(_SFR_IO_ADDR(RAMPZ)));                                    \
    v_; }))
#else
#define tfw_pgm_far_u8(base, p)  pgm_read_byte(p)
#define tfw_pgm_far_u32(base, p) ((uint32_t)pgm_read_dword(p))
#endif
#else
#include <string.h>
#define TFW_PROGMEM        /* nothing: the host has one address space */
#define tfw_pgm_u8(p)      (*(const uint8_t *)(p))
#define tfw_pgm_u16(p)     (*(const uint16_t *)(p))
#define tfw_pgm_u32(p)     (*(const uint32_t *)(p))
#define tfw_pgm_copy(d, s, n) memcpy((d), (s), (n))
#define tfw_pgm_far_u8(base, p)  ((void)(base), *(const uint8_t *)(p))
#define tfw_pgm_far_u32(base, p) ((void)(base), *(const uint32_t *)(p))
#endif

#endif /* TFW_PROGMEM_H */
