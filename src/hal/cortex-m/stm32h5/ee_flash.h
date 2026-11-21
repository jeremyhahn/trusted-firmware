/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's one flash write on the STM32H5 (SPEC row stm32h5): the
 * EEPROM's user sector and nothing else. tfw-boot's flash-interface driver,
 * boot_flash.c's tfw_cm_boot_flash_write, stays boot-only and is not linked
 * into the application; this entry runs h5_flash.c's programming sequence on
 * an offset into the user sector, and refuses any span that leaves it, so no
 * application write reaches a slot, tfw-boot or the platform sector.
 */
#ifndef TFW_H5_EE_FLASH_H
#define TFW_H5_EE_FLASH_H

#include <stdint.h>

#include "tfw/status.h"

struct tfw_h5_flash;

/* The flash interface the write runs on: the part's (ee_flash_port.c), or a
 * host test's register model. */
const struct tfw_h5_flash *tfw_h5_ee_flash(void);

/* Write len bytes at off bytes into the user sector (TFW_CM_USER_EE_BASE,
 * TFW_CM_USER_EE_SIZE). A span past the sector's end is TFW_ERR_RANGE and
 * touches no flash; otherwise h5_flash.c's status. */
tfw_status_t tfw_h5_ee_flash_write(uint32_t off, const uint8_t *data, uint32_t len);

#endif /* TFW_H5_EE_FLASH_H */
