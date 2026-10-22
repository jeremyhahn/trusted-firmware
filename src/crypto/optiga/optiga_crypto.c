/*
 * OPTIGA Trust M as the crypto tier (row optiga-trust-m): the tfw_crypto
 * interface on the read-only command layer, optiga_cmd.c.
 *
 * Every call maps onto a command of the Solution Reference Manual,
 * Revision 3.70 (docs/reference/README.md), or onto TFW_ERR_UNSUPPORTED:
 *
 *   tfw_crypto_init        the transport's open, then OpenApplication
 *                          (§4.4.1.1, Table 40); the context stays open
 *   tfw_crypto_random      GetRandom (§4.4.1.6, Table 45)
 *   tfw_crypto_sha256      the shared software SHA-256, as the ATECC608A tier
 *   tfw_crypto_sign        CalcSign (§4.4.1.12, Table 51), ECDSA with the
 *                          factory key 0xE0F0 (Table 69)
 *   tfw_crypto_counter     GetDataObject (§4.4.1.3, Table 42) on the
 *                          monotonic counters 0xE120-0xE123 (Table 68)
 *   tfw_crypto_hmac        unsupported: HMAC needs a secret written into the
 *                          element first
 *   tfw_crypto_pubkey      unsupported: the key object's public part is not
 *                          readable, and taking it from the device certificate
 *                          0xE0E0 is X.509 parsing, which go-xkms owns
 *   tfw_crypto_verify_operator  VerifySign (§4.4.1.13, Table 52), ECDSA with
 *                          the operator's P-256 key sent in the command
 *   tfw_crypto_verify_image     unsupported: it needs a stored anchor, and
 *                          storing one is a write
 *
 * No key, no secret and no write path: the commands sent are those
 * optiga_cmd.c sends, none of which writes a data object. CalcSign does
 * raise the security event counter (Table 65, "private key use") inside the
 * element; nothing on the host side is written.
 *
 * CRYPTO=optiga links this file (mk/firmware.mk), a tier no board names yet:
 * only the ATmega2560's measurement rows build it. The tier is chosen by the
 * object list, never by #if.
 */
#include <string.h>
#include "optiga_cmd.h"
#include "optiga_i2c.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "../sha256.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"

static uint8_t present;        /* the transport opened and the application answered */

tfw_status_t tfw_crypto_init(void)
{
    present = 0u;

    /* Drains and discards any CDI tfw-boot handed off, as every provider's
     * init does (src/crypto/atecc608a/provider.c says why): this tier signs
     * with the element's factory key, never a CDI-derived one, and the
     * handoff holds the value until something takes it. */
    {
        uint8_t discard[32];
        if (tfw_boot_handoff_take_cdi(discard)) {
            tfw_secure_zero(discard, sizeof discard);
        }
    }

    if (tfw_optiga_open() != TFW_OK) {
        return TFW_OK;          /* no element on this board */
    }
    if (tfw_optiga_open_application() != TFW_OK) {
        return TFW_OK;
    }
    present = 1u;
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return present ? TFW_CRYPTO_SECURE_ELEMENT : TFW_CRYPTO_NONE;
}

uint16_t tfw_crypto_algorithms(void)
{
    if (!present) {
        return 0u;
    }
    /* ECDSA P-256 only: HMAC is not offered (see the file header). */
    return (uint16_t)TFW_ALGBIT_ECDSA_P256_SHA256;
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    uint8_t      rnd[32];
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (len == 0u || len > sizeof rnd) {
        return TFW_ERR_RANGE;
    }
    /* GetRandom gives no fewer than 8 bytes (Table 45); the rest of a short
     * request is cleared, not left on the stack. */
    st = tfw_optiga_get_random(rnd, len < TFW_OPTIGA_RANDOM_MIN ? TFW_OPTIGA_RANDOM_MIN : len);
    if (st == TFW_OK) {
        memcpy(out, rnd, len);
    }
    tfw_secure_zero(rnd, sizeof rnd);
    return st;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    /* Software, as the ATECC608A tier: the hash is over public data, and the
     * element's Hash command would cost a bus round trip per block. */
    tfw_sha256(data, len, out);
    return TFW_OK;
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                             uint8_t out[32])
{
    (void)slot;
    (void)data;
    (void)len;
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64],
                             uint8_t *alg_id)
{
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    st = tfw_optiga_calc_sign(digest, sig);
    if (st == TFW_OK) {
        *alg_id = TFW_ALG_ECDSA_P256_SHA256;
    }
    return st;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    uint8_t      v[4];
    uint16_t     n = 0u;
    tfw_status_t st;

    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    if (idx >= TFW_OPTIGA_COUNTERS) {
        return TFW_ERR_RANGE;       /* the part has four */
    }
    /* The counter value is the object's first four bytes, the threshold the
     * next four (Table 73, "Counter"). Read high byte first, as the manual's
     * two-byte fields are; the byte order of the value is to confirm on the
     * bench. */
    st = tfw_optiga_get_data((uint16_t)(TFW_OPTIGA_OID_COUNTER_FIRST + idx), 0u,
                             (uint8_t)sizeof v, v, &n);
    if (st != TFW_OK) {
        return st;
    }
    if (n != sizeof v) {
        return TFW_ERR_FRAMING;
    }
    *out = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16)
         | ((uint32_t)v[2] << 8) | (uint32_t)v[3];
    return TFW_OK;
}

tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                        const uint8_t sig[64],
                                        const uint8_t pubkey[64])
{
    if (!present) {
        return TFW_ERR_CRYPTO;
    }
    /* VerifySign with the operator's key in the command: the element checks
     * it and keeps nothing. A signature that does not verify is the
     * element's 0x2C, TFW_ERR_CRYPTO as include/tfw/crypto.h asks; a
     * transport failure comes back as the transport returned it. */
    return tfw_optiga_verify_sign(digest, sig, pubkey);
}

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[32], const uint8_t sig[64])
{
    (void)digest;
    (void)sig;
    return TFW_ERR_UNSUPPORTED;
}
