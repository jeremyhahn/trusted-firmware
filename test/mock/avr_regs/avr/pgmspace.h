/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/pgmspace.h> for the host, as wide as src/hal/avr/hal_eeprom.c needs
 * (#163): the near and the far flash copy. Declarations only: the suite
 * defines both over its own model of the flash, so it sees which one a read
 * took and at what address. The part's FLASHEND, which avr-libc's header
 * brings in from the part's io header, comes from the suite's -D. Only on the
 * -I of the suite that tests that unit; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_PGMSPACE_H
#define TFW_TEST_AVR_PGMSPACE_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t uint_farptr_t;

void *memcpy_P(void *dst, const void *src, size_t n);
void *memcpy_PF(void *dst, uint_farptr_t src, size_t n);

/* boot/hal/avr/boot_hal_avr.c: the near and the far byte read. */
uint8_t pgm_read_byte(uint16_t addr);
uint8_t pgm_read_byte_far(uint_farptr_t addr);

#endif
