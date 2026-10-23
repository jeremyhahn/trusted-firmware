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
/* As tfw_boot_platform_store, with the block's flags byte (TFW_BOOT_EE_FLAG_*),
 * which the structure does not model, set to `flags` in the same write. */
void     tfw_boot_platform_store_flags(const tfw_boot_platform_t *p, uint8_t flags);

/* The floor, from whichever authority this build has: the element's monotonic
 * counter where there is one, because it is the only copy that cannot be made
 * to descend, and the reserved EEPROM copy otherwise. TFW_OK with the floor
 * in `*out`; an element that is there and fails the read returns its error,
 * never the EEPROM copy. */
tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out);

/* TFW_OK only when that authority holds `value`; an element error is returned,
 * never covered by an EEPROM write. */
tfw_status_t tfw_boot_floor_store(tfw_boot_platform_t *p, uint32_t value);

/* Settles a confirmed trial: the floor advances to `version`, then GOOD is
 * written. A failed advance leaves the block CONFIRMED, so the next reset
 * retries it. *floor is the floor in force, raised once the advance holds. */
tfw_status_t tfw_boot_settle(tfw_boot_platform_t *p, uint32_t version,
                             uint32_t *floor);

#endif /* TFW_BOOT_STORE_H */
