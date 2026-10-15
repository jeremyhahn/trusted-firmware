/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The HMAC/DS crypto tier: `CRYPTO=hmac-ds`, on an ESP32 part with an HMAC and
 * a Digital Signature peripheral (the ESP32-P4, and the ESP32-C3 Espressif's
 * QEMU emulates). The ADR is docs/multi-arch/adr-esp-idf-integration.md.
 *
 * TWO eFuse keys, neither readable by software once burned:
 *
 *   the UDS      an HMAC_UP key. Layer 0, in the bootloader (boot/hal/esp32/
 *                boot_l0.c), feeds it the digest secure boot verified, through
 *                the HMAC peripheral: CDI = HMAC(UDS, measurement). The CDI
 *                reaches this file through the handoff block, as on the AVR.
 *
 *   the DS key   an HMAC_DOWN_DIGITAL_SIGNATURE key. The DS peripheral derives
 *                from it, in hardware, the AES key that opens the DeviceID
 *                record in the `tfw_id` partition -- an RSA private key that
 *                exists in the clear only inside DS.
 *
 * WHAT THIS TIER CAN DO OVER THE WIRE TODAY: nothing that signs. The DeviceID
 * is RSA because RSA is what DS computes, and an RSA signature does not fit
 * the wire's 64-byte signature field or the alias template's P-256 key, both
 * of which go-xkms's certificate profile reads. Carrying it is a wire
 * extension and a go-xkms profile (the ADR's follow-ups), so until then the
 * algorithm bitmap is empty, ATTEST is off in the board profile and sign and
 * pubkey refuse. CAPS still reports the tier, truthfully: the key store is
 * the part's own hardware, the class TFW_CRYPTO_SECURE_ELEMENT names.
 *
 * What this file does do, on every boot: take the CDI, open the DeviceID
 * record, have DS sign a fixed self-test digest and check the signature with
 * the record's public key. It logs a fingerprint of each -- an HMAC under the
 * CDI and a hash of the public key, never the secrets -- so a reboot can be
 * shown to reproduce the same identity.
 */

#include <string.h>
#include "esp_ds.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/bignum.h"

#include "tfw/crypto.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "crypto/sha256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"
#include "hal_esp32.h"
#include "id_record.h"

static const char *TAG = "tfw-id";

static uint8_t cdi[32];
static uint8_t have_cdi;
static uint8_t have_deviceid;

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

/* EMSA-PKCS1-v1_5 of a SHA-256 digest, as the little-endian integer DS takes. */
static void pkcs1_le(uint8_t *em_le, size_t k, const uint8_t digest[32])
{
    static const uint8_t prefix[19] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
        0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
    };
    size_t i;
    size_t ps = k - 3u - sizeof prefix - 32u;

    /* Built big-endian, from the top, straight into reverse order. */
    em_le[k - 1u] = 0x00u;
    em_le[k - 2u] = 0x01u;
    for (i = 0; i < ps; i++) {
        em_le[k - 3u - i] = 0xFFu;
    }
    em_le[k - 3u - ps] = 0x00u;
    for (i = 0; i < sizeof prefix; i++) {
        em_le[k - 4u - ps - i] = prefix[i];
    }
    for (i = 0; i < 32u; i++) {
        em_le[31u - i] = digest[i];
    }
}

/* s^e mod n == m, all little-endian words of `k` bytes. */
static int rsa_verify_le(const uint8_t *s_le, const uint8_t *m_le, size_t k,
                         const uint8_t *n_le, uint32_t e)
{
    mbedtls_mpi s, n, ee, r, m;
    int ok = 0;

    mbedtls_mpi_init(&s); mbedtls_mpi_init(&n); mbedtls_mpi_init(&ee);
    mbedtls_mpi_init(&r); mbedtls_mpi_init(&m);
    if (mbedtls_mpi_read_binary_le(&s, s_le, k) == 0 &&
        mbedtls_mpi_read_binary_le(&n, n_le, k) == 0 &&
        mbedtls_mpi_read_binary_le(&m, m_le, k) == 0 &&
        mbedtls_mpi_lset(&ee, (mbedtls_mpi_sint)e) == 0 &&
        mbedtls_mpi_exp_mod(&r, &s, &ee, &n, NULL) == 0) {
        ok = mbedtls_mpi_cmp_mpi(&r, &m) == 0;
    }
    mbedtls_mpi_free(&s); mbedtls_mpi_free(&n); mbedtls_mpi_free(&ee);
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&m);
    return ok;
}

/* Opens the DeviceID record and proves DS holds its private half. */
static void deviceid_check(void)
{
    static tfw_id_record_t rec;
    static esp_ds_data_t   ds;
    static uint8_t         msg[TFW_ID_MAX_BYTES];
    static uint8_t         sig[TFW_ID_MAX_BYTES];
    uint8_t                digest[32];
    uint8_t                fp[32];
    char                   fp_hex[33];
    size_t                 k;

    if (tfw_esp_part_read(TFW_ESP_PART_IDENTITY, 0u, (uint8_t *)&rec, sizeof rec) != TFW_OK ||
        rec.magic != TFW_ID_MAGIC || rec.version != TFW_ID_VERSION ||
        rec.rsa_bits == 0u || rec.rsa_bits > ESP_DS_SIGNATURE_MAX_BIT_LEN ||
        rec.rsa_bits % 32u != 0u || rec.ds_key_block >= HMAC_KEY_MAX) {
        ESP_LOGW(TAG, "deviceid: no record in partition %s", TFW_ESP_PART_IDENTITY);
        return;
    }
    k = rec.rsa_bits / 8u;
    tfw_id_fingerprint(&rec, fp);
    hex(fp_hex, fp, 16u);

    ds.rsa_length = (esp_digital_signature_length_t)(rec.rsa_bits / 32u - 1u);
    memcpy(ds.iv, rec.iv, sizeof ds.iv);
    memcpy(ds.c, rec.c, sizeof ds.c);

    tfw_sha256((const uint8_t *)"tfw-ds-selftest-v1", 18u, digest);
    pkcs1_le(msg, k, digest);
    if (esp_ds_sign(msg, &ds, (hmac_key_id_t)rec.ds_key_block, sig) != ESP_OK) {
        ESP_LOGE(TAG, "deviceid %s: DS refused the record", fp_hex);
        return;
    }
    if (!rsa_verify_le(sig, msg, k, rec.n_le, rec.e)) {
        ESP_LOGE(TAG, "deviceid %s: DS signature does not verify", fp_hex);
        return;
    }
    have_deviceid = 1u;
    ESP_LOGI(TAG, "deviceid rsa%u sha256(n||e)=%s ds-selftest=ok",
             (unsigned)rec.rsa_bits, fp_hex);
}

tfw_status_t tfw_crypto_init(void)
{
    const uint8_t *tier = tfw_boot_handoff_tier();

    have_cdi = 0u;
    have_deviceid = 0u;
    /* As on the software tier: a valid handoff for this tier is authoritative,
     * and one for another tier, or none at all, leaves no CDI. There is no
     * local fallback here -- the UDS is in an eFuse only the bootloader's layer
     * 0 feeds a measurement to. */
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
    deviceid_check();
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void) { return TFW_CRYPTO_SECURE_ELEMENT; }

/* Empty until a DS signature has a wire encoding: see the header. */
uint16_t tfw_crypto_algorithms(void) { return 0u; }

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

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    (void)digest; (void)sig; (void)alg_id;
    return have_deviceid ? TFW_ERR_UNSUPPORTED : TFW_ERR_NOT_PROVISIONED;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    (void)out;
    return have_deviceid ? TFW_ERR_UNSUPPORTED : TFW_ERR_NOT_PROVISIONED;
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
