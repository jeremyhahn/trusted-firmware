/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the CDI handoff address, as src/hal/avr/
 * hal_handoff.c is on AVR: a section of its own, NOLOAD, pinned by the
 * linker script to __tfw_handoff, which mk/arch/cortex-m.mk computes from
 * RAM_END by the one formula both images use. tfw-boot's copy
 * (boot/hal/cortex-m/boot_hal_cortex-m.c) is the same section at the same
 * address. Neither image's reset handler copies or zeroes it.
 */

#include "tfw/boot.h"
#include "tfw/hal.h"

static uint8_t handoff[TFW_BOOT_HANDOFF_LEN] __attribute__((section(".handoff")));

uint8_t *tfw_hal_handoff_block(void)
{
    return handoff;
}
