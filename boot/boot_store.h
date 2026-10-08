/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Reserved-EEPROM access and the rollback floor. Shared by the boot path and
 * the OTA recovery path -- see boot_store.c. */
#ifndef TFW_BOOT_STORE_H
#define TFW_BOOT_STORE_H

#include "tfw/status.h"
#include "boot_platform.h"

void     tfw_boot_platform_load(tfw_boot_platform_t *p, tfw_status_t *st);
void     tfw_boot_platform_store(const tfw_boot_platform_t *p);

/* The floor, from whichever authority this build has: the element's monotonic
 * counter where there is one, because it is the only copy that cannot be made
 * to descend, and the reserved EEPROM copy otherwise. */
uint32_t tfw_boot_floor_load(const tfw_boot_platform_t *p);
void     tfw_boot_floor_store(tfw_boot_platform_t *p, uint32_t value);

#endif /* TFW_BOOT_STORE_H */
