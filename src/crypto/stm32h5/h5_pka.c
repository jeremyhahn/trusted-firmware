/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ECDSA P-256 on the STM32H5's PKA (#544), in the order of ST's HAL:
 * HAL_PKA_Init (EN until it reads back, then INITOK), PKA_ECDSASign_Set and
 * PKA_ECDSAVerif_Set (each operand as little-endian words, least significant
 * first, closed by two zero words), PKA_Process (MODE, START, PROCENDF, clear
 * the flags). Register facts: h5_crypto_regs.h.
 */
#include "h5_pka.h"

#include <stddef.h>

#include "h5_crypto_regs.h"

/* NIST P-256 (FIPS 186-4 D.1.2.3), big-endian. a is -3: the PKA takes its
 * sign and its magnitude apart. */
static const uint8_t P256_P[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t P256_N[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};
static const uint8_t P256_B[32] = {
    0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xE7, 0xB3, 0xEB, 0xBD, 0x55, 0x76, 0x98, 0x86, 0xBC,
    0x65, 0x1D, 0x06, 0xB0, 0xCC, 0x53, 0xB0, 0xF6, 0x3B, 0xCE, 0x3C, 0x3E, 0x27, 0xD2, 0x60, 0x4B,
};
static const uint8_t P256_GX[32] = {
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
    0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
};
static const uint8_t P256_GY[32] = {
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
    0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
};
static const uint8_t P256_ABS_A[32] = { [31] = 3 };
#define P256_BITS   256u
#define P256_A_NEG  1u

static int h5_pka_wait(uintptr_t base, uint32_t mask)
{
    for (uint32_t i = 0; i < TFW_H5_POLL_LIMIT; i++) {
        if (tfw_h5_rd(base + TFW_H5_PKA_SR) & mask)
            return 1;
    }
    return 0;
}

/* PKA_Memcpy_u8_to_u32 and __PKA_RAM_PARAM_END for a 32-byte operand. */
static void h5_pka_put(uintptr_t base, uint32_t off, const uint8_t v[32])
{
    for (uint32_t i = 0; i < 8u; i++) {
        const uint8_t *p = &v[28u - 4u * i];
        tfw_h5_wr(base + off + 4u * i, ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                                           ((uint32_t)p[2] << 8) | p[3]);
    }
    tfw_h5_wr(base + off + 32u, 0);
    tfw_h5_wr(base + off + 36u, 0);
}

/* PKA_Memcpy_u32_to_u8 for a 32-byte result. */
static void h5_pka_get(uintptr_t base, uint32_t off, uint8_t v[32])
{
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t w = tfw_h5_rd(base + off + 4u * i);
        uint8_t *p = &v[28u - 4u * i];
        p[0] = (uint8_t)(w >> 24);
        p[1] = (uint8_t)(w >> 16);
        p[2] = (uint8_t)(w >> 8);
        p[3] = (uint8_t)w;
    }
}

/* PKA_Process with the interrupts off: TFW_OK when PROCENDF rose with no
 * error flag. The flags are cleared either way. */
static tfw_status_t h5_pka_run(uintptr_t base, uint32_t mode)
{
    uint32_t cr = TFW_H5_PKA_CR_EN | (mode << TFW_H5_PKA_CR_MODE_POS);

    tfw_h5_wr(base + TFW_H5_PKA_CR, cr);
    tfw_h5_wr(base + TFW_H5_PKA_CR, cr | TFW_H5_PKA_CR_START);
    int done = h5_pka_wait(base, TFW_H5_PKA_SR_PROCENDF);
    uint32_t sr = tfw_h5_rd(base + TFW_H5_PKA_SR);
    tfw_h5_wr(base + TFW_H5_PKA_CLRFR, TFW_H5_PKA_CLRFR_ALL);
    return (done && (sr & TFW_H5_PKA_SR_ERRORS) == 0u) ? TFW_OK : TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_h5_pka_init(uintptr_t base)
{
    uint32_t i = 0;

    while ((tfw_h5_rd(base + TFW_H5_PKA_CR) & TFW_H5_PKA_CR_EN) == 0u) {
        if (i++ == TFW_H5_POLL_LIMIT)
            return TFW_ERR_TIMEOUT;
        tfw_h5_wr(base + TFW_H5_PKA_CR, TFW_H5_PKA_CR_EN);
    }
    if (!h5_pka_wait(base, TFW_H5_PKA_SR_INITOK))
        return TFW_ERR_TIMEOUT;
    tfw_h5_wr(base + TFW_H5_PKA_CLRFR, TFW_H5_PKA_CLRFR_ALL);
    return TFW_OK;
}

tfw_status_t tfw_h5_pka_ecdsa_verify(uintptr_t base, const uint8_t digest[32],
                                     const uint8_t sig[64], const uint8_t pub[64])
{
    tfw_h5_wr(base + TFW_H5_PKA_VERIF_IN_ORDER_NB_BITS, P256_BITS);
    tfw_h5_wr(base + TFW_H5_PKA_VERIF_IN_MOD_NB_BITS, P256_BITS);
    tfw_h5_wr(base + TFW_H5_PKA_VERIF_IN_A_COEFF_SIGN, P256_A_NEG);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_A_COEFF, P256_ABS_A);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_MOD_GF, P256_P);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_INITIAL_POINT_X, P256_GX);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_INITIAL_POINT_Y, P256_GY);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_X, &pub[0]);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_Y, &pub[32]);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_SIGNATURE_R, &sig[0]);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_SIGNATURE_S, &sig[32]);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_HASH_E, digest);
    h5_pka_put(base, TFW_H5_PKA_VERIF_IN_ORDER_N, P256_N);

    tfw_status_t st = h5_pka_run(base, TFW_H5_PKA_MODE_ECDSA_VERIFY);
    if (st != TFW_OK)
        return st;
    return tfw_h5_rd(base + TFW_H5_PKA_VERIF_OUT_RESULT) == TFW_H5_PKA_NO_ERROR ? TFW_OK
                                                                              : TFW_ERR_CRYPTO;
}

tfw_status_t tfw_h5_pka_ecdsa_sign(uintptr_t base, const uint8_t digest[32],
                                   const uint8_t d[32], const uint8_t k[32],
                                   uint8_t sig[64])
{
    tfw_h5_wr(base + TFW_H5_PKA_SIGN_IN_ORDER_NB_BITS, P256_BITS);
    tfw_h5_wr(base + TFW_H5_PKA_SIGN_IN_MOD_NB_BITS, P256_BITS);
    tfw_h5_wr(base + TFW_H5_PKA_SIGN_IN_A_COEFF_SIGN, P256_A_NEG);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_A_COEFF, P256_ABS_A);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_B_COEFF, P256_B);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_MOD_GF, P256_P);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_K, k);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_INITIAL_POINT_X, P256_GX);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_INITIAL_POINT_Y, P256_GY);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_HASH_E, digest);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_PRIVATE_KEY_D, d);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_ORDER_N, P256_N);

    tfw_status_t st = h5_pka_run(base, TFW_H5_PKA_MODE_ECDSA_SIGN);
    /* The PKA keeps d and k in its RAM until the next operation's writes;
     * overwrite them now either way. */
    static const uint8_t zero[32];
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_PRIVATE_KEY_D, zero);
    h5_pka_put(base, TFW_H5_PKA_SIGN_IN_K, zero);
    if (st != TFW_OK)
        return st;
    if (tfw_h5_rd(base + TFW_H5_PKA_SIGN_OUT_ERROR) != TFW_H5_PKA_NO_ERROR)
        return TFW_ERR_CRYPTO;
    h5_pka_get(base, TFW_H5_PKA_SIGN_OUT_SIGNATURE_R, &sig[0]);
    h5_pka_get(base, TFW_H5_PKA_SIGN_OUT_SIGNATURE_S, &sig[32]);
    return TFW_OK;
}
