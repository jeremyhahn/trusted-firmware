/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's hardware tier (CRYPTO=stm32h5, #544): the soft provider with
 * the part's HASH and PKA under it. The soft provider is linked beside this
 * one, its names renamed to tfw_crypto_soft_* as CRYPTO=auto renames them
 * (mk/firmware.mk, ARCH_CRYPTO_WRAPS_SOFT), and keeps everything the keys
 * touch: the identity is derived from the UDS in flash and signed in
 * software, so the tier this reports is the soft one's. What moves is:
 *
 *   tfw_crypto_sha256           the HASH peripheral
 *   tfw_crypto_verify_operator  the PKA's ECDSA verification
 *
 * Each falls back to software when its peripheral did not start, timed out
 * or flagged an error (docs/plans/544.md: a register fact not yet confirmed
 * on the board must not cost an answer). Signing on the PKA waits for a
 * nonce source, which is not this slice's.
 */
#include "tfw/crypto.h"

#include "sha256.h"
#include "soc_regs.h"

#include "h5_hash.h"
#include "h5_pka.h"

tfw_status_t      tfw_crypto_soft_init(void);
tfw_crypto_tier_t tfw_crypto_soft_tier(void);
uint16_t          tfw_crypto_soft_algorithms(void);
tfw_status_t      tfw_crypto_soft_random(uint8_t *out, uint8_t len);
tfw_status_t      tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t      tfw_crypto_soft_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id);
tfw_status_t      tfw_crypto_soft_pubkey(uint8_t out[64]);
tfw_status_t      tfw_crypto_soft_counter(uint8_t idx, uint32_t *out);
tfw_status_t      tfw_crypto_soft_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                                  const uint8_t pubkey[64]);
tfw_status_t      tfw_crypto_soft_verify_image(const uint8_t digest[32], const uint8_t sig[64]);

/* CMSIS stm32h573xx.h: HASH_BASE_NS and PKA_BASE_NS are AHB2PERIPH_BASE_NS
 * plus 0xA0400 and 0xA2000, and GPIOA_BASE_NS is AHB2PERIPH_BASE_NS itself.
 * RCC_AHB2ENR_HASHEN, _RNGEN and _PKAEN are bits 17, 18 and 19. */
#define H5_HASH_BASE        (TFW_H5_GPIOA_BASE + 0xA0400u)
#define H5_PKA_BASE         (TFW_H5_GPIOA_BASE + 0xA2000u)
#define H5_AHB2ENR_HASHEN   (1u << 17)
#define H5_AHB2ENR_RNGEN    (1u << 18)
#define H5_AHB2ENR_PKAEN    (1u << 19)

static uint8_t pka_up;

tfw_status_t tfw_crypto_init(void)
{
    /* The RNG's bus clock with the PKA's; a clock alone writes nothing to the
     * RNG. */
    TFW_H5_RCC_AHB2ENR |= H5_AHB2ENR_HASHEN | H5_AHB2ENR_RNGEN | H5_AHB2ENR_PKAEN;
    (void)TFW_H5_RCC_AHB2ENR;
    /* The PKA reaches INITOK only with the HSI48 running (bench, 2026-11-17);
     * a HSI48 that never readies leaves INITOK low and the soft code answers. */
    (void)tfw_h5_hsi48_start(TFW_H5_RCC_BASE);
    pka_up = (uint8_t)(tfw_h5_pka_init(H5_PKA_BASE) == TFW_OK);
    return tfw_crypto_soft_init();
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return tfw_crypto_soft_tier();
}

uint16_t tfw_crypto_algorithms(void)
{
    return tfw_crypto_soft_algorithms();
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    return tfw_crypto_soft_random(out, len);
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    if (tfw_h5_hash_sha256(H5_HASH_BASE, data, len, out) != TFW_OK) {
        tfw_sha256(data, len, out);
    }
    return TFW_OK;
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    return tfw_crypto_soft_hmac(slot, data, len, out);
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id)
{
    return tfw_crypto_soft_sign(digest, sig, alg_id);
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    return tfw_crypto_soft_pubkey(out);
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    return tfw_crypto_soft_counter(idx, out);
}

tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                        const uint8_t pubkey[64])
{
    if (pka_up) {
        tfw_status_t st = tfw_h5_pka_ecdsa_verify(H5_PKA_BASE, digest, sig, pubkey);
        if (st != TFW_ERR_TIMEOUT) {
            return st;
        }
    }
    return tfw_crypto_soft_verify_operator(digest, sig, pubkey);
}

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[32], const uint8_t sig[64])
{
    return tfw_crypto_soft_verify_image(digest, sig);
}
