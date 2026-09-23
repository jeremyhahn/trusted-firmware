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

static uint8_t ee[AG_BOOT_EE_LEN];

void ag_boot_platform_load(ag_boot_platform_t *p, ag_status_t *st)
{
    *st = ag_boot_eeprom_read(AG_BOOT_EE_BASE, ee, AG_BOOT_EE_LEN);
    if (*st == AG_OK) {
        *st = ag_boot_platform_parse(ee, p);
    }
}

void ag_boot_platform_store(const ag_boot_platform_t *p)
{
    ag_boot_platform_serialise(p, ee);
    (void)ag_boot_eeprom_write(AG_BOOT_EE_BASE, ee, AG_BOOT_EE_LEN);
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
uint32_t ag_boot_floor_load(const ag_boot_platform_t *p)
{
    uint32_t v;

    if (ag_boot_floor_read(&v) == AG_OK) {
        return v;
    }
    return p->rollback_floor;
}

void ag_boot_floor_store(ag_boot_platform_t *p, uint32_t value)
{
    if (ag_boot_floor_write(value) == AG_OK) {
        return;                       /* the element holds it; EEPROM is moot */
    }
    p->rollback_floor = value;
    ag_boot_platform_store(p);
}

