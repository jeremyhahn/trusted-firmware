/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/crypto/stm32h5/h5_pka.c (#544) against a model of the STM32H5's PKA.
 * The model takes the register facts of h5_crypto_regs.h as the
 * peripheral's: it reads each operand back from its RAM offset in ST's
 * layout (little-endian words, least significant first, two zero words after),
 * checks the curve the driver loaded is P-256, and answers with micro-ecc,
 * the soft provider's P-256. So a test passes only if the driver lays out,
 * starts and reads the PKA the way ST's sources say it must.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "uECC.h"

#include "../../src/crypto/stm32h5/h5_crypto_regs.h"
#include "../../src/crypto/stm32h5/h5_pka.h"
#include "tfw/crypto.h"

#define RAM_WORDS  (TFW_H5_PKA_RAM_END / 4u + 1u)
#define FILL       0xA5A5A5A5u
#define REFUSED    0xCBC9u

static struct {
    uint32_t cr;
    uint32_t sr;
    uint32_t ram[RAM_WORDS];
    uint32_t en_delay;      /* EN writes ignored before one sticks */
    uint32_t en_writes;
    int      no_initok;
    int      hang;          /* never raise PROCENDF */
    uint32_t err_flags;     /* SR error flags raised with PROCENDF */
    int      refuse;        /* the sign's error word is not NO_ERROR */
    int      runs;
    uint32_t mode;
    int      fault;
} m;

/* Not static: h5_model/soc_regs.h points the provider's PKA base here. */
uint8_t          regs_anchor[TFW_H5_PKA_RAM_END + 4u];
#define BASE     ((uintptr_t)regs_anchor)

static const uint8_t P[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t N[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};
static const uint8_t B[32] = {
    0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xE7, 0xB3, 0xEB, 0xBD, 0x55, 0x76, 0x98, 0x86, 0xBC,
    0x65, 0x1D, 0x06, 0xB0, 0xCC, 0x53, 0xB0, 0xF6, 0x3B, 0xCE, 0x3C, 0x3E, 0x27, 0xD2, 0x60, 0x4B,
};
static const uint8_t GX[32] = {
    0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
    0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
};
static const uint8_t GY[32] = {
    0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
    0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
};
static const uint8_t ABS_A[32] = { [31] = 3 };

/* An operand as the PKA reads it; a missing terminator is a fault. */
static void ram_get(uint32_t off, uint8_t v[32])
{
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t w = m.ram[off / 4u + i];
        v[28u - 4u * i] = (uint8_t)(w >> 24);
        v[29u - 4u * i] = (uint8_t)(w >> 16);
        v[30u - 4u * i] = (uint8_t)(w >> 8);
        v[31u - 4u * i] = (uint8_t)w;
    }
    if (m.ram[off / 4u + 8u] != 0u || m.ram[off / 4u + 9u] != 0u)
        m.fault = 1;
}

static void ram_put(uint32_t off, const uint8_t v[32])
{
    for (uint32_t i = 0; i < 8u; i++) {
        const uint8_t *p = &v[28u - 4u * i];
        m.ram[off / 4u + i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                              ((uint32_t)p[2] << 8) | p[3];
    }
}

static void expect_operand(uint32_t off, const uint8_t want[32])
{
    uint8_t got[32];

    ram_get(off, got);
    if (memcmp(got, want, 32) != 0)
        m.fault = 1;
}

static void expect_word(uint32_t off, uint32_t want)
{
    if (m.ram[off / 4u] != want)
        m.fault = 1;
}

static void model_verify(void)
{
    uint8_t pub[64], sig[64], e[32];

    expect_word(TFW_H5_PKA_VERIF_IN_ORDER_NB_BITS, 256u);
    expect_word(TFW_H5_PKA_VERIF_IN_MOD_NB_BITS, 256u);
    expect_word(TFW_H5_PKA_VERIF_IN_A_COEFF_SIGN, 1u);
    expect_operand(TFW_H5_PKA_VERIF_IN_A_COEFF, ABS_A);
    expect_operand(TFW_H5_PKA_VERIF_IN_MOD_GF, P);
    expect_operand(TFW_H5_PKA_VERIF_IN_INITIAL_POINT_X, GX);
    expect_operand(TFW_H5_PKA_VERIF_IN_INITIAL_POINT_Y, GY);
    expect_operand(TFW_H5_PKA_VERIF_IN_ORDER_N, N);
    ram_get(TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_X, &pub[0]);
    ram_get(TFW_H5_PKA_VERIF_IN_PUBLIC_KEY_Y, &pub[32]);
    ram_get(TFW_H5_PKA_VERIF_IN_SIGNATURE_R, &sig[0]);
    ram_get(TFW_H5_PKA_VERIF_IN_SIGNATURE_S, &sig[32]);
    ram_get(TFW_H5_PKA_VERIF_IN_HASH_E, e);
    m.ram[TFW_H5_PKA_VERIF_OUT_RESULT / 4u] =
        uECC_verify(pub, e, 32, sig, uECC_secp256r1()) ? TFW_H5_PKA_NO_ERROR : REFUSED;
}

/* micro-ecc draws its own nonce, so the model checks that the driver passed
 * one and signs with d; the test then checks the signature under d's key. */
static void model_sign(void)
{
    static const uint8_t zero[32];
    uint8_t d[32], k[32], e[32], sig[64];

    expect_word(TFW_H5_PKA_SIGN_IN_ORDER_NB_BITS, 256u);
    expect_word(TFW_H5_PKA_SIGN_IN_MOD_NB_BITS, 256u);
    expect_word(TFW_H5_PKA_SIGN_IN_A_COEFF_SIGN, 1u);
    expect_operand(TFW_H5_PKA_SIGN_IN_A_COEFF, ABS_A);
    expect_operand(TFW_H5_PKA_SIGN_IN_B_COEFF, B);
    expect_operand(TFW_H5_PKA_SIGN_IN_MOD_GF, P);
    expect_operand(TFW_H5_PKA_SIGN_IN_INITIAL_POINT_X, GX);
    expect_operand(TFW_H5_PKA_SIGN_IN_INITIAL_POINT_Y, GY);
    expect_operand(TFW_H5_PKA_SIGN_IN_ORDER_N, N);
    ram_get(TFW_H5_PKA_SIGN_IN_K, k);
    ram_get(TFW_H5_PKA_SIGN_IN_PRIVATE_KEY_D, d);
    ram_get(TFW_H5_PKA_SIGN_IN_HASH_E, e);
    if (memcmp(k, zero, 32) == 0)
        m.fault = 1;
    if (m.refuse || !uECC_sign(d, e, 32, sig, uECC_secp256r1())) {
        m.ram[TFW_H5_PKA_SIGN_OUT_ERROR / 4u] = REFUSED;
        return;
    }
    ram_put(TFW_H5_PKA_SIGN_OUT_SIGNATURE_R, &sig[0]);
    ram_put(TFW_H5_PKA_SIGN_OUT_SIGNATURE_S, &sig[32]);
    m.ram[TFW_H5_PKA_SIGN_OUT_ERROR / 4u] = TFW_H5_PKA_NO_ERROR;
}

/* RCC_CR, modelled apart from the PKA: HSI48RDY rises rdy_delay reads after
 * HSI48ON is written, or never with no_rdy. */
#define RCC_OFF 0x10000u
static struct {
    uint32_t cr;
    uint32_t writes;
    uint32_t reads;
    uint32_t rdy_delay;
    uint8_t  no_rdy;
} rcc;

uint32_t tfw_h5_rd(uintptr_t addr)
{
    uint32_t off = (uint32_t)(addr - BASE);

    if (off == RCC_OFF + TFW_H5_RCC_CR_OFF) {
        if ((rcc.cr & TFW_H5_RCC_CR_HSI48ON) && !rcc.no_rdy && rcc.reads++ >= rcc.rdy_delay)
            rcc.cr |= TFW_H5_RCC_CR_HSI48RDY;
        return rcc.cr;
    }

    if (off == TFW_H5_PKA_CR)
        return m.cr;
    if (off == TFW_H5_PKA_SR)
        return m.sr;
    if (off >= TFW_H5_PKA_RAM && off <= TFW_H5_PKA_RAM_END && (off % 4u) == 0u)
        return m.ram[off / 4u];
    m.fault = 1;
    return 0;
}

void tfw_h5_wr(uintptr_t addr, uint32_t value)
{
    uint32_t off = (uint32_t)(addr - BASE);

    if (off == RCC_OFF + TFW_H5_RCC_CR_OFF) {
        rcc.writes++;
        rcc.cr = value & ~TFW_H5_RCC_CR_HSI48RDY;
        return;
    }
    if (off >= TFW_H5_PKA_RAM && off <= TFW_H5_PKA_RAM_END && (off % 4u) == 0u) {
        if (m.sr & TFW_H5_PKA_SR_BUSY)
            m.fault = 1;
        m.ram[off / 4u] = value;
        return;
    }
    switch (off) {
    case TFW_H5_PKA_CR:
        if (value & ~(TFW_H5_PKA_CR_EN | TFW_H5_PKA_CR_START | (0x3Fu << TFW_H5_PKA_CR_MODE_POS)))
            m.fault = 1;
        if ((value & TFW_H5_PKA_CR_EN) && m.cr == 0u && m.en_writes++ < m.en_delay)
            return;
        m.cr = value & ~TFW_H5_PKA_CR_START;
        if ((m.cr & TFW_H5_PKA_CR_EN) && !m.no_initok)
            m.sr |= TFW_H5_PKA_SR_INITOK;
        if (value & TFW_H5_PKA_CR_START) {
            m.mode = (value >> TFW_H5_PKA_CR_MODE_POS) & 0x3Fu;
            m.runs++;
            if (!(m.sr & TFW_H5_PKA_SR_INITOK))
                m.fault = 1;
            if (m.mode == TFW_H5_PKA_MODE_ECDSA_VERIFY)
                model_verify();
            else if (m.mode == TFW_H5_PKA_MODE_ECDSA_SIGN)
                model_sign();
            else
                m.fault = 1;
            if (!m.hang)
                m.sr |= TFW_H5_PKA_SR_PROCENDF | m.err_flags;
        }
        break;
    case TFW_H5_PKA_CLRFR:
        if (value & ~TFW_H5_PKA_CLRFR_ALL)
            m.fault = 1;
        m.sr &= ~value;
        break;
    default:
        m.fault = 1;
    }
}

static uint32_t rng_state = 0x2545F491u;

static int test_rng(uint8_t *dest, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        dest[i] = (uint8_t)rng_state;
    }
    return 1;
}

static uint8_t pub[64], priv[32], digest[32], nonce[32];

void setUp(void)
{
    memset(&m, 0, sizeof m);
    for (uint32_t i = 0; i < RAM_WORDS; i++)
        m.ram[i] = FILL;
    uECC_set_rng(test_rng);
    TEST_ASSERT_TRUE(uECC_make_key(pub, priv, uECC_secp256r1()));
    test_rng(digest, sizeof digest);
    test_rng(nonce, sizeof nonce);
    nonce[0] |= 1u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_init(BASE));
}

void tearDown(void) {}

static void test_h5_pka_init_rewrites_en_until_it_sticks(void)
{
    memset(&m, 0, sizeof m);
    m.en_delay = 3;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_init(BASE));
    TEST_ASSERT_EQUAL_UINT32(4u, m.en_writes);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PKA_CR_EN, m.cr);
    TEST_ASSERT_FALSE(m.fault);
}

static void test_h5_pka_init_times_out_without_initok(void)
{
    memset(&m, 0, sizeof m);
    m.no_initok = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_pka_init(BASE));
}

static void test_h5_hsi48_start_sets_on_keeps_other_bits_and_waits_for_rdy(void)
{
    memset(&rcc, 0, sizeof rcc);
    rcc.cr = 0x00040023u;   /* the kit's RCC_CR before the provider ran */
    rcc.rdy_delay = 5;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_hsi48_start(BASE + RCC_OFF));
    TEST_ASSERT_EQUAL_HEX32(0x00040023u | TFW_H5_RCC_CR_HSI48ON | TFW_H5_RCC_CR_HSI48RDY, rcc.cr);
    TEST_ASSERT_EQUAL(1, rcc.writes);
}

static void test_h5_hsi48_start_writes_nothing_when_already_ready(void)
{
    memset(&rcc, 0, sizeof rcc);
    rcc.cr = TFW_H5_RCC_CR_HSI48ON | TFW_H5_RCC_CR_HSI48RDY;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_hsi48_start(BASE + RCC_OFF));
    TEST_ASSERT_EQUAL(0, rcc.writes);
}

static void test_h5_hsi48_start_times_out_without_rdy(void)
{
    memset(&rcc, 0, sizeof rcc);
    rcc.no_rdy = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_hsi48_start(BASE + RCC_OFF));
}

static void test_h5_pka_init_times_out_when_en_never_sticks(void)
{
    memset(&m, 0, sizeof m);
    m.en_delay = UINT32_MAX;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_pka_init(BASE));
    TEST_ASSERT_EQUAL_UINT32(0u, m.cr);
}

static void test_h5_pka_verifies_a_soft_signature(void)
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
    TEST_ASSERT_FALSE(m.fault);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PKA_MODE_ECDSA_VERIFY, m.mode);
    TEST_ASSERT_EQUAL_UINT32(0u, m.sr & TFW_H5_PKA_CLRFR_ALL);
}

static void test_h5_pka_rejects_a_signature_of_another_digest(void)
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    digest[31] ^= 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
    TEST_ASSERT_FALSE(m.fault);
}

static void test_h5_pka_rejects_a_flipped_s(void)
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    sig[63] ^= 0x80u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
    TEST_ASSERT_FALSE(m.fault);
}

static void test_h5_pka_signs_and_the_soft_verify_accepts_it(void)
{
    static const uint8_t zero[32];
    uint8_t sig[64], left[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_ecdsa_sign(BASE, digest, priv, nonce, sig));
    TEST_ASSERT_FALSE(m.fault);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PKA_MODE_ECDSA_SIGN, m.mode);
    TEST_ASSERT_TRUE(uECC_verify(pub, digest, 32, sig, uECC_secp256r1()));
    /* d and k do not outlive the operation in the PKA's RAM. */
    ram_get(TFW_H5_PKA_SIGN_IN_PRIVATE_KEY_D, left);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, left, 32);
    ram_get(TFW_H5_PKA_SIGN_IN_K, left);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, left, 32);
}

/* Sign on the PKA, verify on the PKA, over several keys and digests. */
static void test_h5_pka_round_trips_its_own_signatures(void)
{
    uint8_t sig[64];

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(uECC_make_key(pub, priv, uECC_secp256r1()));
        test_rng(digest, sizeof digest);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_ecdsa_sign(BASE, digest, priv, nonce, sig));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
    }
    TEST_ASSERT_FALSE(m.fault);
    TEST_ASSERT_EQUAL(16, m.runs);
}

static void test_h5_pka_sign_refused_leaves_sig_alone(void)
{
    uint8_t sig[64];

    memset(sig, 0x5A, sizeof sig);
    m.refuse = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_h5_pka_ecdsa_sign(BASE, digest, priv, nonce, sig));
    for (uint32_t i = 0; i < sizeof sig; i++)
        TEST_ASSERT_EQUAL_HEX8(0x5A, sig[i]);
}

static void test_h5_pka_times_out_when_procendf_never_rises(void)
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_pka_ecdsa_sign(BASE, digest, priv, nonce, sig));
}

/* A RAM, address or operation error is no answer: the caller falls back. */
static void test_h5_pka_error_flags_are_no_answer(void)
{
    uint8_t sig[64];

    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    for (uint32_t bit = 19; bit <= 21; bit++) {
        m.err_flags = 1u << bit;
        TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_pka_ecdsa_verify(BASE, digest, sig, pub));
        TEST_ASSERT_EQUAL_UINT32(0u, m.sr & TFW_H5_PKA_CLRFR_ALL);
    }
    TEST_ASSERT_FALSE(m.fault);
}

/* The provider (src/crypto/stm32h5/provider.c) over the same model. What it
 * wraps of the soft provider is stubbed; its software verify is the real one,
 * src/crypto/soft/verify_p256.c renamed as mk/firmware.mk renames it. */
volatile uint32_t h5_model_ahb2enr;

tfw_status_t      tfw_crypto_soft_init(void) { return TFW_OK; }
tfw_crypto_tier_t tfw_crypto_soft_tier(void) { return TFW_CRYPTO_SOFTWARE; }
uint16_t          tfw_crypto_soft_algorithms(void) { return 0; }
tfw_status_t      tfw_crypto_soft_random(uint8_t *out, uint8_t len)
{
    (void)out; (void)len;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    (void)slot; (void)data; (void)len; (void)out;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_sign(const uint8_t d[32], uint8_t sig[64], uint8_t *alg_id)
{
    (void)d; (void)sig; (void)alg_id;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_pubkey(uint8_t out[64])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_counter(uint8_t idx, uint32_t *out)
{
    (void)idx; (void)out;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_verify_image(const uint8_t d[32], const uint8_t sig[64])
{
    (void)d; (void)sig;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_h5_hash_sha256(uintptr_t base, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    (void)base; (void)data; (void)len; (void)out;
    return TFW_ERR_TIMEOUT;
}
void tfw_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    (void)data; (void)len;
    memset(out, 0, 32);
}

/* The PKA never reaches INITOK: every verify is micro-ecc's, and the PKA is
 * never started. */
static void test_h5_provider_verifies_in_software_when_the_pka_does_not_start(void)
{
    uint8_t sig[64];

    memset(&m, 0, sizeof m);
    m.no_initok = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
    sig[40] ^= 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL(0, m.runs);
}

/* A PKA that never ends its run: the same signature is verified in software. */
static void test_h5_provider_verifies_in_software_when_the_pka_times_out(void)
{
    uint8_t sig[64];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL(1, m.runs);
}

/* A timed-out run may still be going: its late PROCENDF must not answer a
 * later request, so the PKA is not started again this boot and the next
 * verify, of another digest, is micro-ecc's. */
static void test_h5_provider_leaves_a_timed_out_pka_alone(void)
{
    uint8_t sig[64];
    uint8_t other[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_TRUE(uECC_sign(priv, digest, 32, sig, uECC_secp256r1()));
    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
    m.hang = 0;
    m.sr |= TFW_H5_PKA_SR_PROCENDF; /* the late completion */
    memcpy(other, digest, sizeof other);
    other[0] ^= 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(other, sig, pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_operator(digest, sig, pub));
    TEST_ASSERT_EQUAL(1, m.runs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h5_pka_init_rewrites_en_until_it_sticks);
    RUN_TEST(test_h5_pka_init_times_out_without_initok);
    RUN_TEST(test_h5_pka_init_times_out_when_en_never_sticks);
    RUN_TEST(test_h5_hsi48_start_sets_on_keeps_other_bits_and_waits_for_rdy);
    RUN_TEST(test_h5_hsi48_start_writes_nothing_when_already_ready);
    RUN_TEST(test_h5_hsi48_start_times_out_without_rdy);
    RUN_TEST(test_h5_pka_verifies_a_soft_signature);
    RUN_TEST(test_h5_pka_rejects_a_signature_of_another_digest);
    RUN_TEST(test_h5_pka_rejects_a_flipped_s);
    RUN_TEST(test_h5_pka_signs_and_the_soft_verify_accepts_it);
    RUN_TEST(test_h5_pka_round_trips_its_own_signatures);
    RUN_TEST(test_h5_pka_sign_refused_leaves_sig_alone);
    RUN_TEST(test_h5_pka_times_out_when_procendf_never_rises);
    RUN_TEST(test_h5_pka_error_flags_are_no_answer);
    RUN_TEST(test_h5_provider_verifies_in_software_when_the_pka_does_not_start);
    RUN_TEST(test_h5_provider_verifies_in_software_when_the_pka_times_out);
    RUN_TEST(test_h5_provider_leaves_a_timed_out_pka_alone);
    return UNITY_END();
}
