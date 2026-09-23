/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * DICE-SW layer 0: the UDS in boot-section flash, the HMAC in software.
 *
 * This tier is notSecure and says so. What BLB mode 3 buys is that the
 * APPLICATION cannot read the boot section, so the UDS below is out of reach of
 * every command the application exposes -- which is the whole reason
 * docs/security/fuses-and-lockbits.md calls the lock bits "what makes the
 * software tier's UDS worth anything".
 *
 * What it does not buy: anything against someone holding the part. There is no
 * TRNG, no key isolation from bootloader code itself, and no protection against
 * an attacker who can clear the lock bits with a chip erase -- which also
 * erases the flash, so what they get is a blank part rather than the secret.
 * That is the honest boundary and it is the one the tier is rated at.
 *
 * ONE UDS PER IMAGE, PLACED AT PROVISIONING. The symbol below is a placeholder
 * of the right size in the built image; the provisioning jig patches the real
 * secret into it before the bootloader is flashed, exactly as it patches the
 * public key. A secret that came from this source file would be a secret every
 * build of this source shares.
 */

#include <string.h>

#include "../boot_cdi.h"
#include "../boot_hal.h"
#include "ag/boot.h"
#include "ag/progmem.h"
#include "sha256.h"

/*
 * The UDS slot, and the value that means "empty".
 *
 * All-0xFF is erased flash, which is what an unprogrammed part reads and what a
 * chip erase leaves behind. Treating it as a secret would mean every
 * un-provisioned device in a fleet derived the same CDI from the same "key" --
 * so it is the one value this file refuses, and the refusal is what turns a
 * missed provisioning step into an honest AG_ERR_NOT_PROVISIONED instead of a
 * fleet of identical identities.
 */
#define UDS_LEN 32u

static const uint8_t uds_slot[UDS_LEN] AG_PROGMEM = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu
};

ag_status_t ag_boot_cdi_init(void)
{
    return AG_OK;
}

uint8_t ag_boot_cdi_tier(void)
{
    return (uint8_t)AG_CRYPTO_SOFTWARE;
}

ag_status_t ag_boot_cdi_derive(const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                               uint8_t cdi[AG_BOOT_MEASURE_LEN])
{
    uint8_t uds[UDS_LEN];
    uint8_t erased = 0xFFu;
    uint8_t i;

    for (i = 0u; i < UDS_LEN; i++) {
        uds[i]  = (uint8_t)ag_pgm_u8(&uds_slot[i]);
        erased &= uds[i];
    }
    if (erased == 0xFFu) {
        memset(uds, 0, sizeof uds);
        return AG_ERR_NOT_PROVISIONED;
    }

    ag_hmac_sha256(uds, (uint8_t)UDS_LEN, measurement, AG_BOOT_MEASURE_LEN, cdi);

    /* The copy on the stack is the UDS itself, not a derivative. Leaving it for
     * whatever runs next -- including the application, a few hundred
     * instructions later, in the same SRAM -- would hand across the one value
     * BLB mode 3 exists to keep on this side of the barrier. */
    memset(uds, 0, sizeof uds);
    return AG_OK;
}

/*
 * No monotonic counter on this tier, so the floor lives in reserved EEPROM and
 * the caller maintains it there.
 *
 * Reported as AG_ERR_UNSUPPORTED rather than by returning a zero floor. A zero
 * floor is a real, legitimate value -- a device that has never been updated --
 * and a tier with no counter must not be able to masquerade as one whose
 * counter happens to read zero.
 */
ag_status_t ag_boot_floor_read(uint32_t *out)
{
    (void)out;
    return AG_ERR_UNSUPPORTED;
}

ag_status_t ag_boot_floor_write(uint32_t value)
{
    (void)value;
    return AG_ERR_UNSUPPORTED;
}
