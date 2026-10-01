/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's READ-ONLY view of the platform-reserved EEPROM block --
 * issue #6.
 *
 * This links boot/boot_platform.c's ag_boot_platform_parse -- the decode half
 * of the same layout agprov writes and the bootloader also reads -- and calls
 * it over bytes fetched with the application's own ag_hal_eeprom_read. It does
 * not link, and has no way to reach, ag_boot_platform_serialise: an
 * application able to write this block back would be an application able to
 * forge its own device_serial, which is the exact hole
 * src/caps/storage.c's reserved-region refusal exists to close.
 */
#ifndef AG_CORE_PLATFORM_ID_H
#define AG_CORE_PLATFORM_ID_H

#include <stdint.h>
#include "ag/boot.h"

void ag_platform_id_init(void);

/* AG_BOOT_SERIAL_LEN zero bytes when the region is unprovisioned or
 * corrupted -- zero-rather-than-fabricated, the same rule ag_attest_device_id
 * has always followed. */
const uint8_t *ag_platform_id_serial(void);

#endif /* AG_CORE_PLATFORM_ID_H */
