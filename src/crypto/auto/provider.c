/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE MEGA'S RUNTIME TIER -- CRYPTO=auto (issue #130 criterion 1; owner ruling
 * of 2026-10-17, CA m435). The element when one answers, else the soft root.
 * The design, and why it is this shape, is docs/dice-attestation/
 * architecture.md's "The Mega's runtime tier".
 *
 * Both providers are linked. mk/firmware.mk compiles each with a -D rename
 * list (CRYPTO_RENAME), so src/crypto/soft/provider.c defines
 * tfw_crypto_soft_* and src/crypto/atecc608a/provider.c tfw_crypto_element_*,
 * and this file alone defines the public names: one byte chosen at init and
 * one branch per entry. No table of pointers, in RAM or in flash.
 *
 * THE CHOICE IS MADE ONCE AND NEVER CHANGES UNTIL RESET. The probe runs here,
 * before either provider's init, because the element provider drains the boot
 * handoff's CDI unconditionally and the soft provider needs it when tfw-boot
 * derived it from the soft UDS. A probe that fails at any step -- no bus, no
 * wake, a failed or corrupt Info -- runs the soft provider and only it. After
 * that every call goes to the provider chosen: an element that disappears
 * later fails its calls, it never hands them to the soft key.
 */
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "../atecc608a/atecc.h"

tfw_status_t      tfw_crypto_soft_init(void);
tfw_crypto_tier_t tfw_crypto_soft_tier(void);
uint16_t          tfw_crypto_soft_algorithms(void);
tfw_status_t      tfw_crypto_soft_random(uint8_t *out, uint8_t len);
tfw_status_t      tfw_crypto_soft_sha256(const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t      tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t      tfw_crypto_soft_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id);
tfw_status_t      tfw_crypto_soft_pubkey(uint8_t out[64]);
tfw_status_t      tfw_crypto_soft_counter(uint8_t idx, uint32_t *out);
tfw_status_t      tfw_crypto_soft_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                                  const uint8_t pubkey[64]);
tfw_status_t      tfw_crypto_soft_verify_image(const uint8_t digest[32], const uint8_t sig[64]);

tfw_status_t      tfw_crypto_element_init(void);
tfw_crypto_tier_t tfw_crypto_element_tier(void);
uint16_t          tfw_crypto_element_algorithms(void);
tfw_status_t      tfw_crypto_element_random(uint8_t *out, uint8_t len);
tfw_status_t      tfw_crypto_element_sha256(const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t      tfw_crypto_element_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t      tfw_crypto_element_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id);
tfw_status_t      tfw_crypto_element_pubkey(uint8_t out[64]);
tfw_status_t      tfw_crypto_element_counter(uint8_t idx, uint32_t *out);
tfw_status_t      tfw_crypto_element_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                                     const uint8_t pubkey[64]);
tfw_status_t      tfw_crypto_element_verify_image(const uint8_t digest[32], const uint8_t sig[64]);

static uint8_t element;     /* 1: the element answered at init; fixed until reset */

/* The element provider's own presence test, without its handoff drain. */
static uint8_t element_answers(void)
{
    uint8_t rev[4];

    if (tfw_hal_i2c_init(TFW_ATECC_I2C_HZ) != TFW_OK) {
        return 0u;
    }
    if (tfw_atecc_wake() != TFW_OK) {
        return 0u;
    }
    if (tfw_atecc_exec(TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u,
                       0, 0u, rev, 4u, TFW_ATECC_T_INFO) != TFW_OK) {
        (void)tfw_atecc_sleep();
        return 0u;
    }
    (void)tfw_atecc_sleep();
    return 1u;
}

tfw_status_t tfw_crypto_init(void)
{
    element = 0u;
    if (element_answers()) {
        (void)tfw_crypto_element_init();
        element = (uint8_t)(tfw_crypto_element_tier() == TFW_CRYPTO_SECURE_ELEMENT);
    }
    if (!element) {
        (void)tfw_crypto_soft_init();
    }
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return element ? tfw_crypto_element_tier() : tfw_crypto_soft_tier();
}

uint16_t tfw_crypto_algorithms(void)
{
    return element ? tfw_crypto_element_algorithms() : tfw_crypto_soft_algorithms();
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    return element ? tfw_crypto_element_random(out, len) : tfw_crypto_soft_random(out, len);
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    return element ? tfw_crypto_element_sha256(data, len, out)
                   : tfw_crypto_soft_sha256(data, len, out);
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    return element ? tfw_crypto_element_hmac(slot, data, len, out)
                   : tfw_crypto_soft_hmac(slot, data, len, out);
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    return element ? tfw_crypto_element_sign(digest, sig, alg_id)
                   : tfw_crypto_soft_sign(digest, sig, alg_id);
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    return element ? tfw_crypto_element_pubkey(out) : tfw_crypto_soft_pubkey(out);
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    return element ? tfw_crypto_element_counter(idx, out) : tfw_crypto_soft_counter(idx, out);
}

tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                      const uint8_t sig[64],
                                      const uint8_t pubkey[64])
{
    return element ? tfw_crypto_element_verify_operator(digest, sig, pubkey)
                   : tfw_crypto_soft_verify_operator(digest, sig, pubkey);
}

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[32], const uint8_t sig[64])
{
    return element ? tfw_crypto_element_verify_image(digest, sig)
                   : tfw_crypto_soft_verify_image(digest, sig);
}
