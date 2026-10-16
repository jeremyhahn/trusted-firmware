/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_policy.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_verify.h"
#include "boot_hal.h"

uint8_t tfw_boot_equal(const uint8_t *a, const uint8_t *b, uint8_t n)
{
    uint8_t diff = 0u;
    uint8_t i;

    for (i = 0u; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return (uint8_t)(diff == 0u);
}

tfw_status_t tfw_boot_check_pubkey_pin(const uint8_t pinned[TFW_BOOT_MEASURE_LEN],
                                     uint8_t pubkey_out[TFW_BOOT_PUBKEY_LEN])
{
    uint8_t     hash[TFW_BOOT_MEASURE_LEN];
    tfw_status_t st = tfw_boot_pubkey(pubkey_out);

    if (st != TFW_OK) {
        return st;
    }
    tfw_boot_hash(pubkey_out, TFW_BOOT_PUBKEY_LEN, hash);
    if (!tfw_boot_equal(hash, pinned, TFW_BOOT_MEASURE_LEN)) {
        /* The bootloader holds a key this device was not provisioned to trust.
         * Reported as NOT_PROVISIONED rather than CRYPTO because the device is
         * not broken and the image is not bad: the pairing is wrong, and the
         * repair is a provisioning step rather than a new image. */
        return TFW_ERR_NOT_PROVISIONED;
    }
    return TFW_OK;
}

static tfw_status_t flash(uint32_t addr, uint8_t *buf, uint8_t len)
{
    tfw_boot_flash_read(addr, buf, len);
    return TFW_OK;
}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    return tfw_boot_evaluate_at(TFW_BOOT_APP_BASE, floor, out);
}

tfw_status_t tfw_boot_evaluate_at(uint32_t base, uint32_t floor, tfw_boot_verdict_t *out)
{
    const uint32_t tr_at = base + (TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE);
    uint8_t        raw[TFW_BOOT_TRAILER_LEN];
    uint8_t        pubkey[TFW_BOOT_PUBKEY_LEN];
    uint8_t        pinned[TFW_BOOT_MEASURE_LEN];
    uint8_t        ee[TFW_BOOT_EE_LEN];
    const uint8_t *key = pubkey;
    tfw_status_t    st;

    /* The trailer is read in two halves because tfw_boot_flash_read takes a
     * uint8_t length and the trailer is 128 bytes -- a length of 128 is fine,
     * but 256-byte trailers on a later layout would silently truncate, and a
     * loop that cannot express the bug is better than a comment asking nobody
     * to introduce it. */
    tfw_boot_flash_read(tr_at, raw, TFW_BOOT_TRAILER_LEN / 2u);
    tfw_boot_flash_read(tr_at + (TFW_BOOT_TRAILER_LEN / 2u),
                       &raw[TFW_BOOT_TRAILER_LEN / 2u], TFW_BOOT_TRAILER_LEN / 2u);

    st = tfw_boot_trailer_parse(raw, &out->trailer);
    if (st != TFW_OK) {
        return st;
    }

    /* The board binding before the floor: an image for another board is
     * refused whatever its version, and before anything is measured. The id
     * is the profile's -- this file names no board. */
    st = tfw_boot_binding_permit(&out->trailer, (uint8_t)TFW_BOARD_ID);
    if (st != TFW_OK) {
        return st;
    }

    st = tfw_boot_rollback_permit(out->trailer.image_version, floor);
    if (st != TFW_OK) {
        return st;
    }

    st = tfw_boot_measure_from(flash, base, out->trailer.image_len, out->measurement);
    if (st != TFW_OK) {
        return st;
    }
    if (!tfw_boot_equal(out->measurement, out->trailer.measurement,
                       TFW_BOOT_MEASURE_LEN)) {
        /* THE TAMPER CHECK. One byte changed anywhere in the application and
         * this fails, without the signature primitive running at all. */
        return TFW_ERR_CRYPTO;
    }

    /* The pinned hash comes straight from EEPROM rather than from a parsed
     * tfw_boot_platform_t, so that this function has one input -- the floor --
     * and no ordering dependency on who parsed what first. */
    st = tfw_boot_nvm_read(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
    if (st != TFW_OK) {
        return st;
    }
    memcpy(pinned, &ee[TFW_BOOT_EE_O_PUBKEY_HASH], TFW_BOOT_MEASURE_LEN);

    st = tfw_boot_check_pubkey_pin(pinned, pubkey);
    if (st == TFW_ERR_UNSUPPORTED) {
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
         * This is the same shape as tfw_boot_floor_read returning
         * TFW_ERR_UNSUPPORTED on a tier that keeps no floor of its own: a tier
         * capability question, answered once, at the seam that knows.
         */
        key = 0;
    } else if (st != TFW_OK) {
        return st;
    }

    /* The signed span is the trailer's first TFW_BOOT_TR_SIGNED_LEN bytes as
     * they sit in flash -- magic, layout version, board id, image flags, image
     * version, image length and the measurement. Signing the measurement alone would leave the
     * version unauthenticated and hand an attacker anti-rollback for a
     * four-byte edit; see include/tfw/boot.h. */
    return tfw_boot_verify(raw, TFW_BOOT_TR_SIGNED_LEN,
                          out->trailer.signature, key);
}
