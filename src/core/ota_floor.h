#ifndef TFW_OTA_FLOOR_H
#define TFW_OTA_FLOOR_H

/* The rollback floor OTA_INFO reports (#77): the one tfw-boot enforces. On the
 * software tier that is the platform block's copy (ota_floor_eeprom.c); on the
 * element tier it is the element's monotonic counter (ota_floor_atecc.c), and
 * the block's copy is stale once an update has advanced the counter. One file
 * is linked per image, chosen by the crypto tier in mk/firmware.mk. `*out` is
 * written only on TFW_OK; an element that cannot be read is TFW_ERR_CRYPTO,
 * never the block's value. */

#include <stdint.h>
#include "tfw/boot.h"

tfw_status_t tfw_otactl_floor(const tfw_boot_platform_t *p, uint32_t *out);

#endif /* TFW_OTA_FLOOR_H */
