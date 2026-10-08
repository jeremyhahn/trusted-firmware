/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The reserved-EEPROM accessors and the rollback floor.
 *
 * Their own translation unit because BOTH the boot path and the OTA recovery
 * path need them: main() reads the floor before deciding, and the recovery
 * path advances it after an image it received has verified. They were statics
 * in boot_main.c, which worked only while recovery lived in the same file.
 */

#include "boot_store.h"
#include "boot_hal.h"
#include "boot_image.h"
#include "boot_cdi.h"

static uint8_t ee[TFW_BOOT_EE_LEN];

void tfw_boot_platform_load(tfw_boot_platform_t *p, tfw_status_t *st)
{
    *st = tfw_boot_eeprom_read(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
    if (*st == TFW_OK) {
        *st = tfw_boot_platform_parse(ee, p);
    }
}

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    tfw_boot_platform_serialise(p, ee);
    (void)tfw_boot_eeprom_write(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
}

/*
 * The floor, from whichever authority this build has.
 *
 * The element's counter wins where there is one, because it is the only copy
 * that cannot be made to descend. Without an element the EEPROM copy is all
 * there is, and it is exactly as trustworthy as the BODLEVEL setting that stops
 * a sagging supply from corrupting it -- which is why
 * docs/security/fuses-and-lockbits.md calls brown-out detection a security
 * setting on this part.
 */
uint32_t tfw_boot_floor_load(const tfw_boot_platform_t *p)
{
    uint32_t v;

    if (tfw_boot_floor_read(&v) == TFW_OK) {
        return v;
    }
    return p->rollback_floor;
}

void tfw_boot_floor_store(tfw_boot_platform_t *p, uint32_t value)
{
    if (tfw_boot_floor_write(value) == TFW_OK) {
        return;                       /* the element holds it; EEPROM is moot */
    }
    p->rollback_floor = value;
    tfw_boot_platform_store(p);
}

