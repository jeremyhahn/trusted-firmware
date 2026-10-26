/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What tfw-boot's Cortex-M core (boot_hal_cortex-m.c) asks of the SoC's
 * directory, boot/hal/cortex-m/<soc>/, beyond the boot_hal.h entries that
 * directory defines itself (the UART), and its flash write. The watchdog is not here: both images
 * share it through src/hal/cortex-m/hal_early.h.
 */
#ifndef TFW_BOOT_SOC_H
#define TFW_BOOT_SOC_H

#include <stdint.h>

#include "tfw/status.h"

/* The UART back to its reset state, its last byte sent first: the jump calls
 * this, so the application finds the peripheral as a bare reset leaves it
 * (boot/boot_hal.h, tfw_boot_jump_application). */
void tfw_cm_boot_uart_off(void);

/* Writes `len` bytes of RAM at the absolute address `addr` in the part's code
 * storage, and returns when they read back. The caller has checked the range
 * (boot_hal_cortex-m.c); this is the part's program sequence (rp2350 through
 * its boot ROM, nrf54l's RRAM controller), or a copy where the code regions are RAM (mps2-tz under
 * QEMU). */
tfw_status_t tfw_cm_boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/* The factory copies of a two-slot board (BOOT_AB=1): the application and the
 * platform block copied from where QEMU loads them to where they live, each
 * only while its home is blank (boot_factory.c). Empty on one slot
 * (boot_factory_none.c). */
void tfw_cm_boot_factory(void);

#endif /* TFW_BOOT_SOC_H */
