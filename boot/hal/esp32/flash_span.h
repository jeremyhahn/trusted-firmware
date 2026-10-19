/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The aligned window under one piece of an unaligned flash read.
 *
 * ESP-IDF's bootloader_flash_read refuses a source address, a length or a
 * destination that is not 4-byte aligned. The boot HAL contract reads at any
 * byte for any length (the SUIT envelope's 2-byte length after the trailer is
 * one), so boot_hal_esp32.c reads each piece into an aligned bounce buffer and
 * copies the asked-for bytes out. This is the address arithmetic of that
 * piece, split from the read so the host can test it.
 *
 * Rounding stays inside the part: a partition starts and ends on a 4 KB
 * sector, so a window rounded to 4 bytes never leaves the flash chip.
 */
#ifndef TFW_BOOT_FLASH_SPAN_H
#define TFW_BOOT_FLASH_SPAN_H

#include <stdint.h>

typedef struct {
    uint32_t from;  /* the window's start: addr rounded down to 4 */
    uint32_t len;   /* the window's length: a multiple of 4, at most cap */
    uint32_t skip;  /* the bytes before addr in the window, 0..3 */
    uint32_t take;  /* the asked-for bytes this window holds, at least 1 */
} tfw_flash_span_t;

/* The first window of a read of `want` bytes at `addr` through a bounce
 * buffer of `cap` bytes. `want` is not 0; `cap` is a multiple of 4, at least
 * 8, so every window carries at least one asked-for byte. The caller advances
 * addr by take and repeats while bytes remain. */
void tfw_flash_span(uint32_t addr, uint32_t want, uint32_t cap, tfw_flash_span_t *s);

#endif
