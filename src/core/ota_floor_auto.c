/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* CRYPTO=auto's rollback floor (issue #130): read from where tfw-boot keeps
 * it, which is the boot's tier, never the application's own probe. tfw-boot
 * enforces the element's boot counter when it booted on the element and the
 * platform block's floor when it booted soft, and it says which in the
 * handoff's tier byte (include/tfw/boot.h, offset 5). A probe that failed
 * transiently here must not report the EEPROM copy, which can be stale
 * (ota_floor.h), while the counter is the floor in force.
 *
 * mk/firmware.mk compiles ota_floor_atecc.c and ota_floor_eeprom.c with
 * FLOOR_RENAME, so they are tfw_otactl_floor_element and _soft. An element
 * boot whose counter cannot be read fails with that read's TFW_ERR_CRYPTO; no
 * handoff, or a tier this image has no floor for, is TFW_ERR_CRYPTO too: the
 * floor in force cannot be named, so none is reported. The application reads
 * the floor and never writes it; tfw-boot owns both stores. */
#include "tfw/crypto.h"
#include "handoff.h"
#include "ota_floor.h"

tfw_status_t tfw_otactl_floor_element(const tfw_boot_platform_t *p, uint32_t *out);
tfw_status_t tfw_otactl_floor_soft(const tfw_boot_platform_t *p, uint32_t *out);

tfw_status_t tfw_otactl_floor(const tfw_boot_platform_t *p, uint32_t *out)
{
    const uint8_t *boot_tier = tfw_boot_handoff_tier();

    if (boot_tier && *boot_tier == (uint8_t)TFW_CRYPTO_SECURE_ELEMENT) {
        return tfw_otactl_floor_element(p, out);
    }
    if (boot_tier && *boot_tier == (uint8_t)TFW_CRYPTO_SOFTWARE) {
        return tfw_otactl_floor_soft(p, out);
    }
    return TFW_ERR_CRYPTO;
}
