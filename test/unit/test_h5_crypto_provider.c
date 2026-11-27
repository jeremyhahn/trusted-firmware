/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 provider's SHA-256 (#544, #557 Codex P1) through its own entry
 * point, tfw_crypto_sha256, with the real HASH driver (h5_hash.c) and the real
 * software SHA-256 (sha256.c) linked: a HASH peripheral that never finishes
 * still answers the right digest from the software, and one that does answers
 * the same digest from its HR registers. The PKA and the soft provider are
 * stubs: this suite is about the HASH route alone. It also pins init's clock
 * enables: HASH, RNG and PKA clocked, other banks kept, before the HSI48 and
 * the PKA start.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/crypto.h"
#include "h5_vendor_facts.h"
#include "../../src/crypto/stm32h5/h5_hash.h"
#include "../../src/crypto/stm32h5/h5_pka.h"

/* test/unit/h5_model/soc_regs.h names these: the provider's HASH base is
 * GPIOA + 0xA0400, so regs_anchor - 0x1C00. No register is dereferenced. */
uint8_t           regs_anchor[64];
volatile uint32_t h5_model_ahb2enr;
#define HASH_BASE ((uintptr_t)regs_anchor - 0x1C00u)

/* SHA-256("abc"), FIPS 180-4 appendix B.1. */
static const uint8_t ABC_DIGEST[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

/* A HASH model: bytes go in through DIN (byte datatype, first byte low),
 * DCAL raises DCIS with HR holding the digest of ST's vector for the message,
 * or, hung, never does. Its digest is the vector's, not sha256.c's, so the
 * hardware route does not pass by computing in software. */
static struct {
    uint8_t  msg[8];
    uint32_t words;
    uint32_t nblw;
    uint8_t  done;
    uint8_t  hang;
    uint8_t  fault;
    uint32_t hr_reads;
    uint32_t sr_reads;
} m;

uint32_t tfw_h5_rd(uintptr_t addr)
{
    uint32_t off = (uint32_t)(addr - HASH_BASE);

    if (off == ST_HASH_SR) {
        m.sr_reads++;
        return m.done ? ST_HASH_SR_DCIS : (m.hang ? ST_HASH_SR_BUSY : 0u);
    }
    if (off >= ST_HASH_HR && off < ST_HASH_HR + 32u && (off % 4u) == 0u) {
        const uint8_t *d = &ABC_DIGEST[off - ST_HASH_HR];

        m.hr_reads++;
        if (!m.done)
            m.fault = 1;
        return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    }
    m.fault = 1;
    return 0;
}

void tfw_h5_wr(uintptr_t addr, uint32_t value)
{
    uint32_t off = (uint32_t)(addr - HASH_BASE);

    switch (off) {
    case ST_HASH_CR:
        m.words = 0;
        m.done  = 0;
        break;
    case ST_HASH_DIN:
        if (m.words * 4u >= sizeof m.msg) {
            m.fault = 1;
            return;
        }
        for (uint32_t b = 0; b < 4u; b++)
            m.msg[m.words * 4u + b] = (uint8_t)(value >> (8u * b));
        m.words++;
        break;
    case ST_HASH_STR:
        m.nblw = value & ST_HASH_STR_NBLW;
        /* Only "abc" is modelled: one word, 24 valid bits. */
        if ((value & ST_HASH_STR_DCAL) && !m.hang) {
            if (m.words != 1u || m.nblw != 24u || memcmp(m.msg, "abc", 3) != 0)
                m.fault = 1;
            m.done = 1;
        }
        break;
    case ST_HASH_SR:
        if (!(value & ST_HASH_SR_DCIS))
            m.done = 0;
        break;
    default:
        m.fault = 1;
    }
}

/* The PKA and the soft provider, out of this suite's way. The HSI48 and PKA
 * starts record the RCC AHB2ENR they find, so a case can check init clocked
 * the peripherals before it started them. */
static uint32_t ahb2enr_at_hsi48, ahb2enr_at_pka_init;
static unsigned hsi48_calls, pka_init_calls;

tfw_status_t tfw_h5_pka_init(uintptr_t base)
{
    (void)base;
    ahb2enr_at_pka_init = h5_model_ahb2enr;
    pka_init_calls++;
    return TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_h5_hsi48_start(uintptr_t rcc_base)
{
    (void)rcc_base;
    ahb2enr_at_hsi48 = h5_model_ahb2enr;
    hsi48_calls++;
    return TFW_ERR_TIMEOUT;
}
tfw_status_t tfw_h5_pka_ecdsa_verify(uintptr_t base, const uint8_t digest[32],
                                     const uint8_t sig[64], const uint8_t pub[64])
{
    (void)base; (void)digest; (void)sig; (void)pub;
    return TFW_ERR_TIMEOUT;
}
tfw_status_t tfw_h5_pka_ecdsa_sign(uintptr_t base, const uint8_t digest[32],
                                   const uint8_t d[32], const uint8_t k[32], uint8_t sig[64])
{
    (void)base; (void)digest; (void)d; (void)k; (void)sig;
    return TFW_ERR_TIMEOUT;
}
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
tfw_status_t tfw_crypto_soft_verify_p256(const uint8_t digest[32], const uint8_t sig[64],
                                         const uint8_t pubkey[64])
{
    (void)digest; (void)sig; (void)pubkey;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_verify_image(const uint8_t d[32], const uint8_t sig[64])
{
    (void)d; (void)sig;
    return TFW_ERR_UNSUPPORTED;
}

/* Each case is a fresh boot: tfw_crypto_init brings the HASH back. */
void setUp(void)
{
    memset(&m, 0, sizeof m);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}
void tearDown(void) {}

static void test_h5_provider_sha256_falls_back_to_software_on_a_hash_timeout(void)
{
    uint8_t out[32];

    memset(out, 0xA5, sizeof out);
    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(0, m.hr_reads);
    TEST_ASSERT_FALSE(m.fault);
}

static void test_h5_provider_sha256_answers_from_the_hash_peripheral(void)
{
    uint8_t out[32];

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(8, m.hr_reads);
    TEST_ASSERT_FALSE(m.fault);
}

/* #599: a hung HASH costs the status-poll timeout once per boot; after it
 * the digests run in software without touching the peripheral. */
static void test_h5_provider_sha256_polls_a_hung_hash_once_per_boot(void)
{
    uint8_t  out[32];
    uint32_t polls;

    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, sizeof out);
    polls = m.sr_reads;
    TEST_ASSERT_NOT_EQUAL_UINT32(0, polls);

    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(polls, m.sr_reads);
    TEST_ASSERT_EQUAL_UINT32(0, m.hr_reads);
    TEST_ASSERT_FALSE(m.fault);
}

static void test_h5_provider_sha256_uses_the_hash_again_after_init(void)
{
    uint8_t out[32];

    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));

    memset(&m, 0, sizeof m);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    memset(out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABC_DIGEST, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT32(8, m.hr_reads);
    TEST_ASSERT_FALSE(m.fault);
}

/* CMSIS stm32h573xx.h: RCC_AHB2ENR_HASHEN_Pos 17, RCC_AHB2ENR_RNGEN_Pos 18,
 * RCC_AHB2ENR_PKAEN_Pos 19. A HASH or PKA whose bus clock is off reads and
 * writes nothing, so init sets all three, by OR, before it starts either. */
#define ST_RCC_AHB2ENR_HASHEN (1u << 17)
#define ST_RCC_AHB2ENR_RNGEN  (1u << 18)
#define ST_RCC_AHB2ENR_PKAEN  (1u << 19)
#define ST_RCC_AHB2ENR_GPIOAEN (1u << 0)

static void test_h5_provider_init_clocks_hash_rng_and_pka_before_starting_them(void)
{
    const uint32_t want = ST_RCC_AHB2ENR_HASHEN | ST_RCC_AHB2ENR_RNGEN | ST_RCC_AHB2ENR_PKAEN;

    h5_model_ahb2enr = ST_RCC_AHB2ENR_GPIOAEN;
    ahb2enr_at_hsi48 = 0;
    ahb2enr_at_pka_init = 0;
    hsi48_calls = 0;
    pka_init_calls = 0;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_HEX32(want | ST_RCC_AHB2ENR_GPIOAEN, h5_model_ahb2enr);
    TEST_ASSERT_EQUAL_UINT(1, hsi48_calls);
    TEST_ASSERT_EQUAL_UINT(1, pka_init_calls);
    TEST_ASSERT_EQUAL_HEX32(want, ahb2enr_at_hsi48 & want);
    TEST_ASSERT_EQUAL_HEX32(want, ahb2enr_at_pka_init & want);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h5_provider_sha256_falls_back_to_software_on_a_hash_timeout);
    RUN_TEST(test_h5_provider_sha256_answers_from_the_hash_peripheral);
    RUN_TEST(test_h5_provider_sha256_polls_a_hung_hash_once_per_boot);
    RUN_TEST(test_h5_provider_sha256_uses_the_hash_again_after_init);
    RUN_TEST(test_h5_provider_init_clocks_hash_rng_and_pka_before_starting_them);
    return UNITY_END();
}
