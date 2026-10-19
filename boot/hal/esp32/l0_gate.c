/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "l0_gate.h"
#include "tfw/progmem.h"
#include "boot_hal.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_platform.h"
#include "boot_policy.h"
#include "boot_verify.h"
#include "suit.h"

/* The image key, X || Y: p256_key_slot.c's slot, which the ESP32 build
 * generates with the key in it (mk/arch/esp32-targets.mk) because the
 * bootloader is RSA-signed and cannot be patched after the build. */
extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

/* The SUIT envelope that follows the trailer (design section 2.2): its length,
 * little-endian, then the envelope, in the `room` bytes left in the slot.
 * src/core/suit.c reads it and holds it against what ESP-IDF measured and the
 * trailer just verified. tfw_suit_gate's ota_state goes to the local copy of
 * the platform block only: layer 0 writes no NVM. */
static tfw_status_t manifest(uint32_t at, uint32_t room, tfw_boot_platform_t *plat,
                             const tfw_boot_trailer_t *tr,
                             const uint8_t image_digest[TFW_BOOT_MEASURE_LEN],
                             uint32_t image_len)
{
    uint8_t  env[TFW_SUIT_ENVELOPE_MAX];
    uint8_t  n[2];
    uint16_t len;
    uint16_t off;
    uint16_t k;

    if (room < 2u) {
        return TFW_ERR_RANGE;
    }
    tfw_boot_flash_read(at, n, 2u);
    len = (uint16_t)(n[0] | ((uint16_t)n[1] << 8));
    /* Erased flash reads 0xFFFF: no envelope is a refusal, as no key is. */
    if (len == 0u || len > TFW_SUIT_ENVELOPE_MAX || len > room - 2u) {
        return TFW_ERR_RANGE;
    }
    /* In pieces: the HAL takes a uint8_t length. */
    for (off = 0u; off < len; off = (uint16_t)(off + k)) {
        k = (uint16_t)(len - off);
        if (k > 128u) {
            k = 128u;
        }
        tfw_boot_flash_read(at + 2u + off, &env[off], (uint8_t)k);
    }
    return tfw_suit_gate(plat, tr, image_digest, image_len, env, len);
}

tfw_status_t tfw_l0_gate(uint32_t base, uint32_t slot_size, uint32_t esp_len,
                         const uint8_t image_digest[TFW_BOOT_MEASURE_LEN],
                         uint32_t floor, tfw_boot_trailer_t *out)
{
    uint8_t      raw[TFW_BOOT_TRAILER_LEN];
    uint8_t      ee[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;
    uint8_t      held[TFW_BOOT_MEASURE_LEN];
    tfw_status_t st;
    uint8_t      half;

    if (esp_len <= TFW_BOOT_MEASURE_LEN || esp_len > slot_size
        || slot_size - esp_len < TFW_BOOT_TRAILER_LEN) {
        return TFW_ERR_RANGE;
    }
    /* Two halves, as tfw_boot_evaluate_at reads it: the HAL takes a uint8_t
     * length. */
    for (half = 0u; half < TFW_BOOT_TRAILER_LEN; half = (uint8_t)(half + TFW_BOOT_TRAILER_LEN / 2u)) {
        tfw_boot_flash_read(base + esp_len + half, &raw[half], TFW_BOOT_TRAILER_LEN / 2u);
    }

    st = tfw_boot_trailer_parse(raw, out);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_boot_binding_permit(out, (uint8_t)TFW_BOARD_ID);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_boot_rollback_permit(out->image_version, floor);
    if (st != TFW_OK) {
        return st;
    }

    /* The trailer names the span ESP-IDF hashed, the image without its
     * appended digest, and that digest. ESP-IDF has just checked the digest
     * against the image, so nothing is hashed twice. */
    if (out->image_len != esp_len - TFW_BOOT_MEASURE_LEN
        || !tfw_boot_equal(out->measurement, image_digest, TFW_BOOT_MEASURE_LEN)) {
        return TFW_ERR_CRYPTO;
    }

    /* The pin comes out of the platform block as tfw-boot reads it: magic,
     * layout and CRC before any field is believed. It is SHA-256 over the
     * 64-byte key in the slot, checked here rather than through
     * tfw_boot_check_pubkey_pin, which asks the backend for a 32-byte key --
     * p256-soft keeps a 64-byte one and answers TFW_ERR_UNSUPPORTED. The pin is
     * what stops a rewritten bootloader from bringing its own key.
     * NOT_PROVISIONED for the reason that function gives: the pairing is
     * wrong, not the image. */
    st = tfw_boot_nvm_read(TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
    if (st == TFW_OK) {
        st = tfw_boot_platform_parse(ee, &plat);
    }
    if (st != TFW_OK) {
        return st;
    }
    tfw_boot_hash(tfw_boot_p256_key, (uint16_t)sizeof tfw_boot_p256_key, held);
    if (!tfw_boot_equal(held, plat.pubkey_hash, TFW_BOOT_MEASURE_LEN)) {
        return TFW_ERR_NOT_PROVISIONED;
    }

    /* The key goes NULL: p256-soft verifies with its own slot only. */
    st = tfw_boot_verify(raw, TFW_BOOT_TR_SIGNED_LEN, out->signature, 0);
    if (st != TFW_OK) {
        return st;
    }
    return manifest(base + esp_len + TFW_BOOT_TRAILER_LEN,
                    slot_size - esp_len - TFW_BOOT_TRAILER_LEN,
                    &plat, out, image_digest, esp_len - TFW_BOOT_MEASURE_LEN);
}

tfw_status_t tfw_l0_authorized(const tfw_boot_trailer_t *tr, const uint8_t rec[TFW_BOOT_AUTH_LEN])
{
    uint8_t msg[TFW_BOOT_COSE_ACTIVATE_MAX];
    uint8_t mode = rec[TFW_BOOT_AUTH_O_MODE];
    uint8_t len;

    if (tfw_boot_rd_u32(&rec[TFW_BOOT_AUTH_O_MAGIC]) != TFW_BOOT_AUTH_MAGIC || mode > 1u) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    len = tfw_boot_cose_activate(msg, tr->measurement, tr->image_version, mode);
    /* The key the trailer verified with, and the pin tfw_l0_gate checked. */
    if (tfw_boot_verify(msg, len, &rec[TFW_BOOT_AUTH_O_SIG], 0) != TFW_OK) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    return TFW_OK;
}
