/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The platform-block accessors, over the boot HAL's NVM, and the rollback floor.
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
#include "boot_trial.h"

static uint8_t ee[TFW_BOOT_EE_LEN];
static uint8_t loaded;

void tfw_boot_platform_load(tfw_boot_platform_t *p, tfw_status_t *st)
{
    *st = tfw_boot_nvm_read(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
    if (*st == TFW_OK) {
        *st = tfw_boot_platform_parse(ee, p);
    }
    loaded = (uint8_t)(*st == TFW_OK);
}

/*
 * Writes only over a block this boot loaded and parsed. A block that failed
 * its CRC -- a reset in the middle of one of these writes leaves exactly that
 * -- reaches recovery with a zeroed struct, and serialising that over the raw
 * bytes would replace the pinned key hash, the serial and the operator key
 * with zeros. Refusing keeps the torn block as it is, for a programmer to
 * repair, rather than turning it into an unprovisioned one.
 */
static tfw_status_t platform_write(const tfw_boot_platform_t *p)
{
    if (!loaded) {
        return TFW_ERR_BAD_STATE;
    }
    tfw_boot_platform_serialise(p, ee);
    return tfw_boot_nvm_write(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
}

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    (void)platform_write(p);
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
 *
 * The same contract as the store: TFW_ERR_UNSUPPORTED from the tier means
 * there is no element, and only then is the EEPROM copy the floor. An element
 * that is there and fails the read is an error, never the EEPROM copy -- that
 * copy is not the authority on an element build, and an image checked against
 * it is checked against nothing. `*out` is written only on TFW_OK.
 */
tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out)
{
    tfw_status_t st = tfw_boot_floor_read(out);

    if (st != TFW_ERR_UNSUPPORTED) {
        return st;                    /* the element is the authority */
    }
    *out = p->rollback_floor;
    return TFW_OK;
}

/*
 * TFW_OK only when the authority holds `value`. An element that answers
 * TFW_ERR_UNSUPPORTED is not there, so the EEPROM copy is the authority and is
 * written. Any other element error -- a NACK, a CRC, a counter that will not
 * reach the target -- is returned as it is, and EEPROM is NOT written: on this
 * tier the element's counter is the floor, and a higher EEPROM copy beside a
 * lower counter would only make the advance look done when it is not.
 * The EEPROM copy is the authority only once it is written: a block this boot
 * did not load is refused (TFW_ERR_BAD_STATE) and a failed write is returned,
 * and either way `p->rollback_floor` keeps the floor EEPROM still holds.
 */
tfw_status_t tfw_boot_floor_store(tfw_boot_platform_t *p, uint32_t value)
{
    tfw_boot_platform_t next;
    tfw_status_t        st = tfw_boot_floor_write(value);

    if (st != TFW_ERR_UNSUPPORTED) {
        return st;                    /* the element is the authority */
    }
    next = *p;
    next.rollback_floor = value;
    st = platform_write(&next);
    if (st == TFW_OK) {
        p->rollback_floor = value;
    }
    return st;
}

/*
 * The settle of a confirmed trial (decision 5): the floor advances to the
 * version of the image that proved it runs, and only then is GOOD written.
 *
 * A floor the authority did not take leaves the block CONFIRMED and unwritten,
 * and the failure is returned: the next reset settles again and retries the
 * advance. Until one succeeds every reset does -- a confirmed update is never
 * recorded as settled with the old floor still in force. *floor is raised only
 * once the authority holds the new value; the image this boot runs was
 * verified against the old one, and is at or above the new one besides.
 */
tfw_status_t tfw_boot_settle(tfw_boot_platform_t *p, uint32_t version,
                             uint32_t *floor)
{
    uint32_t next;

    (void)tfw_boot_trial_settle(p, version, *floor, &next);
    if (next != *floor) {
        tfw_status_t st = tfw_boot_floor_store(p, next);

        if (st != TFW_OK) {
            p->boot_state = TFW_BOOT_STATE_CONFIRMED;   /* not GOOD: retried */
            return st;
        }
        *floor = next;
    }
    tfw_boot_platform_store(p);
    return TFW_OK;
}

