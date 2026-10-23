/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The HMAC/P-256 crypto tier: `CRYPTO=hmac-p256`, on an ESP32 part with an HMAC
 * peripheral (the ESP32-P4, and the ESP32-C3 Espressif's QEMU emulates). The
 * design is docs/multi-arch/esp32-p4-design.md section 2.1, option (a); the
 * ADR is docs/multi-arch/adr-esp-idf-integration.md.
 *
 * ONE eFuse key, an HMAC_UP key, the UDS. Software never reads it; it asks the
 * HMAC peripheral for a MAC under it, twice over:
 *
 *   the CDI       HMAC(UDS, measurement), computed by layer 0 in the bootloader
 *                 (boot/hal/esp32/boot_l0.c) over the digest secure boot
 *                 verified. It reaches this file through the handoff block, as
 *                 on the AVR.
 *
 *   the DeviceID  HMAC(UDS, "tfw-deviceid-v1"), computed here, is the seed of a
 *                 P-256 key (src/crypto/deviceid_p256.c, the derivation every
 *                 such root shares). It does not depend on the measurement, so
 *                 it survives every update; it changes only with the UDS.
 *
 * The scalar exists for one call, on the stack, and is zeroed before the call
 * returns, as is the seed. Signing is ES256 with RFC 6979's deterministic k,
 * so it needs no entropy; the part's RNG only blinds micro-ecc's arithmetic,
 * which changes no signature.
 *
 * The DS peripheral and its RSA DeviceID record are retired (design 2.1): the
 * wire and go-xkms's profile carry P-256 only. The `tfw_id` partition and its
 * subtype 0x41 stay reserved and are not read.
 */

#include <string.h>
#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_log.h"
#include "esp_random.h"
#include "uECC.h"

#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "crypto/sha256.h"
#include "crypto/deviceid_p256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"
#include "hal_esp32.h"

static const char *TAG = "tfw-id";

static uint8_t       cdi[32];
static uint8_t       have_cdi;
static uint8_t       have_uds;
static hmac_key_id_t uds_key;
static uint8_t       deviceid_pub[64];

static void hex(char *out, const uint8_t *in, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < n; i++) {
        out[2 * i]     = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

/* Blinding for micro-ecc's point arithmetic; never the nonce. */
static int blind_rng(uint8_t *dest, unsigned size)
{
    esp_fill_random(dest, size);
    return 1;
}

/* The DeviceID seed, HMAC(UDS, label), from the HMAC peripheral. */
static int deviceid_seed(uint8_t seed[32])
{
    return esp_hmac_calculate(uds_key, TFW_DEVICEID_LABEL, TFW_DEVICEID_LABEL_LEN,
                              seed) == ESP_OK;
}

/* Finds the UDS, the eFuse key block whose purpose is HMAC_UP -- the one layer 0
 * computes the CDI under -- and derives the DeviceID's public point from it. */
static void deviceid_init(void)
{
    esp_efuse_block_t block;
    uint8_t           seed[32];
    uint8_t           fp[32];
    char              fp_hex[33];

    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &block)) {
        ESP_LOGW(TAG, "deviceid: no HMAC_UP key in eFuse: this part has no UDS");
        return;
    }
    uds_key = (hmac_key_id_t)(block - EFUSE_BLK_KEY0);
    if (!deviceid_seed(seed)) {
        ESP_LOGE(TAG, "deviceid: HMAC peripheral refused key block %d", (int)block);
        tfw_secure_zero(seed, sizeof seed);
        return;
    }
    if (!tfw_deviceid_p256_public(seed, deviceid_pub)) {
        ESP_LOGE(TAG, "deviceid: micro-ecc refused the DeviceID scalar");
        tfw_secure_zero(seed, sizeof seed);
        return;
    }
    tfw_secure_zero(seed, sizeof seed);
    have_uds = 1u;
    tfw_sha256(deviceid_pub, (uint16_t)sizeof deviceid_pub, fp);
    hex(fp_hex, fp, 16u);
    ESP_LOGI(TAG, "deviceid p256 sha256(x||y)=%s from eFuse BLOCK%d", fp_hex, (int)block);
}

tfw_status_t tfw_crypto_init(void)
{
    const uint8_t *tier = tfw_boot_handoff_tier();

    have_cdi = 0u;
    have_uds = 0u;
    uECC_set_rng(blind_rng);
    /* As on the software tier: a valid handoff for this tier is authoritative,
     * and one for another tier, or none at all, leaves no CDI. There is no
     * local fallback here -- the CDI is a MAC over a measurement only the
     * bootloader's layer 0 holds. */
    if (tier && *tier == (uint8_t)TFW_CRYPTO_SECURE_ELEMENT && tfw_boot_handoff_take_cdi(cdi)) {
        uint8_t fp[32];
        char    fp_hex[33];

        have_cdi = 1u;
        tfw_hmac_sha256(cdi, (uint8_t)sizeof cdi,
                        (const uint8_t *)"tfw-cdi-fingerprint-v1", 22u, fp);
        hex(fp_hex, fp, 16u);
        ESP_LOGI(TAG, "cdi hmac-fingerprint=%s", fp_hex);
        tfw_secure_zero(fp, sizeof fp);
    } else if (tier) {
        ESP_LOGW(TAG, "cdi: none in the handoff (tier %u)", (unsigned)*tier);
    } else {
        ESP_LOGW(TAG, "cdi: no valid handoff from layer 0");
    }
    deviceid_init();
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void) { return TFW_CRYPTO_SECURE_ELEMENT; }

/* ES256 with a UDS to derive the DeviceID from; HMAC with a CDI. */
uint16_t tfw_crypto_algorithms(void)
{
    return (uint16_t)((have_uds ? TFW_ALGBIT_ECDSA_P256_SHA256 : 0u) |
                      (have_cdi ? TFW_ALGBIT_HMAC_SHA256 : 0u));
}

/* The part's hardware RNG. */
tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    esp_fill_random(out, len);
    return TFW_OK;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    tfw_sha256(data, len, out);
    return TFW_OK;
}

/* HMAC under the CDI, as the software tier's slot 0 is. */
tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    if (slot != 0u) {
        return TFW_ERR_RANGE;
    }
    if (!have_cdi) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    tfw_hmac_sha256(cdi, (uint8_t)sizeof cdi, data, len, out);
    return TFW_OK;
}

/* ES256 with the DeviceID. As on the element tier, a part whose boot handed
 * off no CDI does not sign: "this device attested" must mean the measured
 * chain ran, not only that the eFuse holds a key. */
tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    uint8_t seed[32];

    if (!have_uds || !have_cdi) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (!deviceid_seed(seed)) {
        tfw_secure_zero(seed, sizeof seed);
        return TFW_ERR_CRYPTO;
    }
    if (!tfw_deviceid_p256_sign(seed, digest, sig)) {
        tfw_secure_zero(seed, sizeof seed);
        return TFW_ERR_CRYPTO;
    }
    tfw_secure_zero(seed, sizeof seed);
    *alg_id = TFW_ALG_ECDSA_P256_SHA256;
    return TFW_OK;
}

/* The point derived at init: public, so it is kept. */
tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    if (!have_uds) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    memcpy(out, deviceid_pub, sizeof deviceid_pub);
    return TFW_OK;
}

/* The eFuse secure_version is ESP-IDF's anti-rollback counter; exposing it
 * here is the OTA row's work, not this one's. */
tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    (void)idx; (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                       const uint8_t sig[64],
                                       const uint8_t pubkey[64])
{
    (void)digest; (void)sig; (void)pubkey;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[32], const uint8_t sig[64])
{
    (void)digest; (void)sig;
    return TFW_ERR_UNSUPPORTED;
}
