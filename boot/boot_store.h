/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Reserved-EEPROM access and the rollback floor. Shared by the boot path and
 * the OTA recovery path -- see boot_store.c. */
#ifndef AG_BOOT_STORE_H
#define AG_BOOT_STORE_H

#include "ag/status.h"
#include "boot_platform.h"

void     ag_boot_platform_load(ag_boot_platform_t *p, ag_status_t *st);
void     ag_boot_platform_store(const ag_boot_platform_t *p);

/* The floor, from whichever authority this build has: the element's monotonic
 * counter where there is one, because it is the only copy that cannot be made
 * to descend, and the reserved EEPROM copy otherwise. */
uint32_t ag_boot_floor_load(const ag_boot_platform_t *p);
void     ag_boot_floor_store(ag_boot_platform_t *p, uint32_t value);

#endif /* AG_BOOT_STORE_H */
