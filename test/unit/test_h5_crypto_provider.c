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
/* The soft tier the provider forwards to (#599): each stub records what it
 * was called with in soft_seen, answers soft_st (TFW_ERR_UNSUPPORTED unless a
 * case sets it) and fills its out buffer with SOFT_PAT, so a provider that
 * answers without calling it, swaps an argument or invents a status fails. */
#define SOFT_PAT 0x5Cu

static struct {
    unsigned       calls;
    const uint8_t *digest;
    const uint8_t *sig_in;
    uint8_t       *sig;
    uint8_t       *alg_id;
    uint8_t       *out;
    uint32_t      *out32;
    const uint8_t *data;
    uint16_t       len;
    uint8_t        slot;
    uint8_t        idx;
} soft_seen;
static tfw_status_t      soft_st;
static tfw_crypto_tier_t soft_tier_value;
static uint16_t          soft_algorithms_value;

tfw_status_t      tfw_crypto_soft_init(void) { return TFW_OK; }
tfw_crypto_tier_t tfw_crypto_soft_tier(void) { return soft_tier_value; }
uint16_t          tfw_crypto_soft_algorithms(void) { return soft_algorithms_value; }
tfw_status_t      tfw_crypto_soft_random(uint8_t *out, uint8_t len)
{
    soft_seen.calls++;
    soft_seen.out = out;
    soft_seen.len = len;
    memset(out, SOFT_PAT, len);
    return soft_st;
}
tfw_status_t tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32])
{
    soft_seen.calls++;
    soft_seen.slot = slot;
    soft_seen.data = data;
    soft_seen.len  = len;
    soft_seen.out  = out;
    memset(out, SOFT_PAT, 32);
    return soft_st;
}
tfw_status_t tfw_crypto_soft_sign(const uint8_t d[32], uint8_t sig[64], uint8_t *alg_id)
{
    soft_seen.calls++;
    soft_seen.digest = d;
    soft_seen.sig    = sig;
    soft_seen.alg_id = alg_id;
    memset(sig, SOFT_PAT, 64);
    return soft_st;
}
tfw_status_t tfw_crypto_soft_pubkey(uint8_t out[64])
{
    soft_seen.calls++;
    soft_seen.out = out;
    memset(out, SOFT_PAT, 64);
    return soft_st;
}
tfw_status_t tfw_crypto_soft_counter(uint8_t idx, uint32_t *out)
{
    soft_seen.calls++;
    soft_seen.idx   = idx;
    soft_seen.out32 = out;
    *out = 0x5C5C5C5Cu;
    return soft_st;
}
tfw_status_t tfw_crypto_soft_verify_p256(const uint8_t digest[32], const uint8_t sig[64],
                                         const uint8_t pubkey[64])
{
    (void)digest; (void)sig; (void)pubkey;
    return TFW_ERR_UNSUPPORTED;
}
tfw_status_t tfw_crypto_soft_verify_image(const uint8_t d[32], const uint8_t sig[64])
{
    soft_seen.calls++;
    soft_seen.digest = d;
    soft_seen.sig_in = sig;
    return soft_st;
}

/* Each case is a fresh boot: tfw_crypto_init brings the HASH back. */
void setUp(void)
{
    memset(&m, 0, sizeof m);
    memset(&soft_seen, 0, sizeof soft_seen);
    soft_st               = TFW_ERR_UNSUPPORTED;
    soft_tier_value       = TFW_CRYPTO_SOFTWARE;
    soft_algorithms_value = 0;
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

/* #599: the STM32H5 image check is the soft tier's, not the PKA's. Its
 * refusal is the provider's answer, with the digest and signature handed
 * over in that order. */
static void test_h5_provider_verify_image_returns_the_soft_tiers_refusal(void)
{
    uint8_t digest[32], sig[64];

    memset(digest, 0x11, sizeof digest);
    memset(sig, 0x22, sizeof sig);
    soft_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_image(digest, sig));
    TEST_ASSERT_EQUAL_UINT(1, soft_seen.calls);
    TEST_ASSERT_EQUAL_PTR(digest, soft_seen.digest);
    TEST_ASSERT_EQUAL_PTR(sig, soft_seen.sig_in);

    soft_st = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_image(digest, sig));
    TEST_ASSERT_EQUAL_UINT(2, soft_seen.calls);
}

static void test_h5_provider_sign_and_pubkey_answer_from_the_soft_tier(void)
{
    uint8_t digest[32], sig[64], pub[64], alg_id = 0;
    uint8_t pat[64];

    memset(pat, SOFT_PAT, sizeof pat);
    memset(digest, 0x11, sizeof digest);
    memset(sig, 0, sizeof sig);
    soft_st = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_sign(digest, sig, &alg_id));
    TEST_ASSERT_EQUAL_UINT(1, soft_seen.calls);
    TEST_ASSERT_EQUAL_PTR(digest, soft_seen.digest);
    TEST_ASSERT_EQUAL_PTR(sig, soft_seen.sig);
    TEST_ASSERT_EQUAL_PTR(&alg_id, soft_seen.alg_id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pat, sig, sizeof sig);
    soft_st = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg_id));
    TEST_ASSERT_EQUAL_UINT(2, soft_seen.calls);

    memset(pub, 0, sizeof pub);
    soft_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT(3, soft_seen.calls);
    TEST_ASSERT_EQUAL_PTR(pub, soft_seen.out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pat, pub, sizeof pub);
    soft_st = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(pub));
    TEST_ASSERT_EQUAL_UINT(4, soft_seen.calls);
}

/* Two distinct statuses per call, so a provider returning a constant fails. */
static void test_h5_provider_hmac_counter_and_random_pass_their_arguments_through(void)
{
    static const tfw_status_t sts[2] = {TFW_ERR_CRYPTO, TFW_OK};
    const uint8_t data[5] = {1, 2, 3, 4, 5};
    uint8_t       mac[32], rnd[16], pat[32];
    uint32_t      ctr;

    memset(pat, SOFT_PAT, sizeof pat);
    for (unsigned i = 0; i < 2u; i++) {
        soft_st = sts[i];

        memset(&soft_seen, 0, sizeof soft_seen);
        memset(mac, 0, sizeof mac);
        TEST_ASSERT_EQUAL(sts[i], tfw_crypto_hmac((uint8_t)(3u + i), data, (uint16_t)(sizeof data - i), mac));
        TEST_ASSERT_EQUAL_UINT(1, soft_seen.calls);
        TEST_ASSERT_EQUAL_UINT8(3u + i, soft_seen.slot);
        TEST_ASSERT_EQUAL_PTR(data, soft_seen.data);
        TEST_ASSERT_EQUAL_UINT16(sizeof data - i, soft_seen.len);
        TEST_ASSERT_EQUAL_PTR(mac, soft_seen.out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(pat, mac, sizeof mac);

        memset(&soft_seen, 0, sizeof soft_seen);
        ctr = 0;
        TEST_ASSERT_EQUAL(sts[i], tfw_crypto_counter((uint8_t)(2u + i), &ctr));
        TEST_ASSERT_EQUAL_UINT(1, soft_seen.calls);
        TEST_ASSERT_EQUAL_UINT8(2u + i, soft_seen.idx);
        TEST_ASSERT_EQUAL_PTR(&ctr, soft_seen.out32);
        TEST_ASSERT_EQUAL_HEX32(0x5C5C5C5Cu, ctr);

        memset(&soft_seen, 0, sizeof soft_seen);
        memset(rnd, 0, sizeof rnd);
        TEST_ASSERT_EQUAL(sts[i], tfw_crypto_random(rnd, (uint8_t)(sizeof rnd - i)));
        TEST_ASSERT_EQUAL_UINT(1, soft_seen.calls);
        TEST_ASSERT_EQUAL_PTR(rnd, soft_seen.out);
        TEST_ASSERT_EQUAL_UINT16(sizeof rnd - i, soft_seen.len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(pat, rnd, sizeof rnd - i);
    }
}

static void test_h5_provider_tier_and_algorithms_are_the_soft_tiers(void)
{
    soft_tier_value       = TFW_CRYPTO_SOFTWARE;
    soft_algorithms_value = 0x0003u;
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(0x0003u, tfw_crypto_algorithms());

    soft_tier_value       = TFW_CRYPTO_NONE;
    soft_algorithms_value = 0x0140u;
    TEST_ASSERT_EQUAL(TFW_CRYPTO_NONE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(0x0140u, tfw_crypto_algorithms());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h5_provider_sha256_falls_back_to_software_on_a_hash_timeout);
    RUN_TEST(test_h5_provider_sha256_answers_from_the_hash_peripheral);
    RUN_TEST(test_h5_provider_sha256_polls_a_hung_hash_once_per_boot);
    RUN_TEST(test_h5_provider_sha256_uses_the_hash_again_after_init);
    RUN_TEST(test_h5_provider_init_clocks_hash_rng_and_pka_before_starting_them);
    RUN_TEST(test_h5_provider_verify_image_returns_the_soft_tiers_refusal);
    RUN_TEST(test_h5_provider_sign_and_pubkey_answer_from_the_soft_tier);
    RUN_TEST(test_h5_provider_hmac_counter_and_random_pass_their_arguments_through);
    RUN_TEST(test_h5_provider_tier_and_algorithms_are_the_soft_tiers);
    return UNITY_END();
}
