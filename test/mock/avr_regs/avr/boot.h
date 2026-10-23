/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/boot.h> for the host, one unit wide (#163): the self-programming
 * calls boot/hal/avr/boot_hal_avr.c makes, as calls the suite defines over
 * its own model of the page buffer and the flash, so it sees each SPM step,
 * its order and its address. The part's SPM_PAGESIZE, which avr-libc brings
 * in from the part's io header, comes from the suite's -D. Only on the -I of
 * the suite that tests that unit; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_BOOT_H
#define TFW_TEST_AVR_BOOT_H

#include <stdint.h>

void boot_page_erase(uint32_t addr);
void boot_page_fill(uint32_t addr, uint16_t word);
void boot_page_write(uint32_t addr);
void boot_spm_busy_wait(void);
void boot_rww_enable(void);

#endif
