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
#include "tfw/boot.h"
#include "tfw/progmem.h"
#include "sha256.h"
#include "deviceid_p256.h"

/*
 * The UDS slot, and the value that means "empty".
 *
 * All-0xFF is erased flash, which is what an unprogrammed part reads and what a
 * chip erase leaves behind. Treating it as a secret would mean every
 * un-provisioned device in a fleet derived the same CDI from the same "key" --
 * so it is the one value this file refuses, and the refusal is what turns a
 * missed provisioning step into an honest TFW_ERR_NOT_PROVISIONED instead of a
 * fleet of identical identities.
 */
#define UDS_LEN 32u

static const uint8_t uds_slot[UDS_LEN] TFW_PROGMEM = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu
};

tfw_status_t tfw_boot_cdi_init(void)
{
    return TFW_OK;
}

uint8_t tfw_boot_cdi_tier(void)
{
    return (uint8_t)TFW_CRYPTO_SOFTWARE;
}

/*
 * HMAC(UDS, msg) into `out`: the CDI and the DeviceID seed are this with two
 * messages. One function rather than the UDS read and the HMAC written out in
 * each saves 116 bytes of the 2560's soft tfw-boot (#173). `out` is untouched
 * when there is no UDS.
 */
static tfw_status_t uds_hmac(const uint8_t *msg, uint8_t msg_len, uint8_t out[32])
{
    uint8_t      uds[UDS_LEN];
    uint8_t      erased = 0xFFu;
    uint8_t      i;
    tfw_status_t st     = TFW_ERR_NOT_PROVISIONED;

    for (i = 0u; i < UDS_LEN; i++) {
        uds[i]  = (uint8_t)tfw_pgm_far_u8(uds_slot, &uds_slot[i]);
        erased &= uds[i];
    }
    if (erased != 0xFFu) {
        tfw_hmac_sha256(uds, (uint8_t)UDS_LEN, msg, msg_len, out);
        st = TFW_OK;
    }

    /* The copy on the stack is the UDS itself, not a derivative. Leaving it for
     * whatever runs next -- including the application, a few hundred
     * instructions later, in the same SRAM -- would hand across the one value
     * BLB mode 3 exists to keep on this side of the barrier. */
    memset(uds, 0, sizeof uds);
    return st;
}

tfw_status_t tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                               uint8_t cdi[TFW_BOOT_MEASURE_LEN])
{
    return uds_hmac(measurement, TFW_BOOT_MEASURE_LEN, cdi);
}

/* The same derivation src/crypto/soft/id_p256.c made from the UDS before the
 * UDS stayed on this side: the application now takes the result from the
 * handoff, and the key it signs with is unchanged. */
tfw_status_t tfw_boot_cdi_device_seed(uint8_t seed[TFW_BOOT_SEED_LEN])
{
    tfw_status_t st = uds_hmac((const uint8_t *)TFW_DEVICEID_LABEL, TFW_DEVICEID_LABEL_LEN, seed);

    if (st != TFW_OK) {
        memset(seed, 0, TFW_BOOT_SEED_LEN);
    }
    return st;
}

/*
 * No monotonic counter on this tier, so the floor lives in reserved EEPROM and
 * the caller maintains it there.
 *
 * Reported as TFW_ERR_UNSUPPORTED rather than by returning a zero floor. A zero
 * floor is a real, legitimate value -- a device that has never been updated --
 * and a tier with no counter must not be able to masquerade as one whose
 * counter happens to read zero.
 */
tfw_status_t tfw_boot_floor_read(uint32_t *out)
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_boot_floor_write(uint32_t value)
{
    (void)value;
    return TFW_ERR_UNSUPPORTED;
}
