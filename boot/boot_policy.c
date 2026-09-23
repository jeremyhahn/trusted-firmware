/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_policy.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_verify.h"
#include "boot_hal.h"

uint8_t ag_boot_equal(const uint8_t *a, const uint8_t *b, uint8_t n)
{
    uint8_t diff = 0u;
    uint8_t i;

    for (i = 0u; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return (uint8_t)(diff == 0u);
}

ag_status_t ag_boot_check_pubkey_pin(const uint8_t pinned[AG_BOOT_MEASURE_LEN],
                                     uint8_t pubkey_out[AG_BOOT_PUBKEY_LEN])
{
    uint8_t     hash[AG_BOOT_MEASURE_LEN];
    ag_status_t st = ag_boot_pubkey(pubkey_out);

    if (st != AG_OK) {
        return st;
    }
    ag_boot_hash(pubkey_out, AG_BOOT_PUBKEY_LEN, hash);
    if (!ag_boot_equal(hash, pinned, AG_BOOT_MEASURE_LEN)) {
        /* The bootloader holds a key this device was not provisioned to trust.
         * Reported as NOT_PROVISIONED rather than CRYPTO because the device is
         * not broken and the image is not bad: the pairing is wrong, and the
         * repair is a provisioning step rather than a new image. */
        return AG_ERR_NOT_PROVISIONED;
    }
    return AG_OK;
}

ag_status_t ag_boot_evaluate(uint32_t floor, ag_boot_verdict_t *out)
{
    uint8_t        raw[AG_BOOT_TRAILER_LEN];
    uint8_t        pubkey[AG_BOOT_PUBKEY_LEN];
    uint8_t        pinned[AG_BOOT_MEASURE_LEN];
    uint8_t        ee[AG_BOOT_EE_LEN];
    const uint8_t *key = pubkey;
    ag_status_t    st;

    /* The trailer is read in two halves because ag_boot_flash_read takes a
     * uint8_t length and the trailer is 128 bytes -- a length of 128 is fine,
     * but 256-byte trailers on a later layout would silently truncate, and a
     * loop that cannot express the bug is better than a comment asking nobody
     * to introduce it. */
    ag_boot_flash_read(AG_BOOT_TRAILER_ADDR, raw, AG_BOOT_TRAILER_LEN / 2u);
    ag_boot_flash_read(AG_BOOT_TRAILER_ADDR + (AG_BOOT_TRAILER_LEN / 2u),
                       &raw[AG_BOOT_TRAILER_LEN / 2u], AG_BOOT_TRAILER_LEN / 2u);

    st = ag_boot_trailer_parse(raw, &out->trailer);
    if (st != AG_OK) {
        return st;
    }

    st = ag_boot_rollback_permit(out->trailer.image_version, floor);
    if (st != AG_OK) {
        return st;
    }

    st = ag_boot_measure(out->trailer.image_len, out->measurement);
    if (st != AG_OK) {
        return st;
    }
    if (!ag_boot_equal(out->measurement, out->trailer.measurement,
                       AG_BOOT_MEASURE_LEN)) {
        /* THE TAMPER CHECK. One byte changed anywhere in the application and
         * this fails, without the signature primitive running at all. */
        return AG_ERR_CRYPTO;
    }

    /* The pinned hash comes straight from EEPROM rather than from a parsed
     * ag_boot_platform_t, so that this function has one input -- the floor --
     * and no ordering dependency on who parsed what first. */
    st = ag_boot_eeprom_read(AG_BOOT_EE_BASE, ee, AG_BOOT_EE_LEN);
    if (st != AG_OK) {
        return st;
    }
    memcpy(pinned, &ee[AG_BOOT_EE_O_PUBKEY_HASH], AG_BOOT_MEASURE_LEN);

    st = ag_boot_check_pubkey_pin(pinned, pubkey);
    if (st == AG_ERR_UNSUPPORTED) {
        /*
         * THE BACKEND KEEPS THE KEY AND WILL NOT HAND IT OVER. That is the
         * element tier working as designed, not a failure: the signing key sits
         * in a locked element slot and is named by number, so there is no
         * 32-byte value for this function to hash and nothing for the EEPROM
         * pin to compare against.
         *
         * Nor is the pin needed there. The pin exists because replacing the
         * bootloader replaces the key it verifies against -- and an element
         * whose slot is locked cannot have its key replaced at all, by a
         * rewritten boot section or by anything else on this part. Skipping a
         * check the hardware has already made stronger is not a weaker device.
         *
         * `key` goes NULL rather than pointing at an untouched buffer, because
         * the element backend REFUSES a caller-supplied key -- honouring one
         * would mean verifying against whatever an attacker who reached the
         * flash had put there, which is the whole thing stored mode prevents.
         *
         * This is the same shape as ag_boot_floor_read returning
         * AG_ERR_UNSUPPORTED on a tier that keeps no floor of its own: a tier
         * capability question, answered once, at the seam that knows.
         */
        key = 0;
    } else if (st != AG_OK) {
        return st;
    }

    /* The signed span is the trailer's first AG_BOOT_TR_SIGNED_LEN bytes as
     * they sit in flash -- magic, layout version, image version, image length
     * and the measurement. Signing the measurement alone would leave the
     * version unauthenticated and hand an attacker anti-rollback for a
     * four-byte edit; see include/ag/boot.h. */
    return ag_boot_verify(raw, AG_BOOT_TR_SIGNED_LEN,
                          out->trailer.signature, key);
}
