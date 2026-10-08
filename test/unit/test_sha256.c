/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SHA-256 and HMAC-SHA256 against the published vectors.
 *
 * These are the only property that matters for a hash: agreement with every
 * other implementation in the world. A hash that is self-consistent and wrong
 * produces a device whose measurements no verifier can reproduce.
 */

#include <string.h>
#include "unity.h"
#include "sha256.h"
#include "vectors.h"

void setUp(void) {}
void tearDown(void) {}

static void hex(const uint8_t *b, uint8_t n, char *out)
{
    static const char D[] = "0123456789abcdef";
    for (uint8_t i = 0u; i < n; i++) {
        out[i * 2u]      = D[b[i] >> 4];
        out[i * 2u + 1u] = D[b[i] & 0x0Fu];
    }
    out[n * 2u] = '\0';
}

static void expect(const uint8_t *data, uint16_t len, const char *want)
{
    uint8_t d[32];
    char    got[65];
    tfw_sha256(data, len, d);
    hex(d, 32u, got);
    TEST_ASSERT_EQUAL_STRING(want, got);
}

static void test_sha256_of_abc(void)
{
    expect((const uint8_t *)TFWV_SHA256_ABC_INPUT, 3u, TFWV_SHA256_ABC);
}

/* The empty input. A padding routine that special-cases a length of zero gets
 * this wrong, and a great many do. */
static void test_sha256_of_the_empty_input(void)
{
    expect((const uint8_t *)"", 0u, TFWV_SHA256_EMPTY);
}

/* 448 bits forces the padding to spill into a second block -- the case an
 * implementation that only ever hashes short inputs passes everything else
 * and fails here. */
static void test_sha256_where_the_padding_spills_into_a_second_block(void)
{
    expect((const uint8_t *)TFWV_SHA256_448_INPUT,
           (uint16_t)strlen(TFWV_SHA256_448_INPUT), TFWV_SHA256_448);
}

/* Streaming must equal one-shot. The firmware hashes its own flash in chunks,
 * so if these diverge the measurement depends on the chunk size -- and no
 * verifier could reproduce it. */
static void test_streaming_equals_one_shot(void)
{
    const uint8_t *msg = (const uint8_t *)TFWV_SHA256_448_INPUT;
    uint16_t       n   = (uint16_t)strlen(TFWV_SHA256_448_INPUT);
    uint8_t        a[32], b[32];
    tfw_sha256_t    c;

    tfw_sha256(msg, n, a);

    tfw_sha256_init(&c);
    for (uint16_t i = 0u; i < n; i++) {     /* one byte at a time */
        tfw_sha256_update(&c, &msg[i], 1u);
    }
    tfw_sha256_final(&c, b);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 32);
}

/* A hash must be sensitive to every bit. Not a proof, but it catches a
 * transform that drops a word -- which self-consistency would hide. */
static void test_a_single_bit_change_changes_the_digest(void)
{
    uint8_t in[64], a[32], b[32];

    for (uint8_t i = 0u; i < sizeof in; i++) { in[i] = i; }
    tfw_sha256(in, sizeof in, a);
    in[31] ^= 0x01u;
    tfw_sha256(in, sizeof in, b);

    TEST_ASSERT_FALSE(memcmp(a, b, 32) == 0);
}

/* RFC 4231 test case 1. The DICE one-way function is HMAC, so the whole
 * identity chain rests on this being right. */
static void test_hmac_sha256_rfc4231_case_1(void)
{
    uint8_t key[TFWV_HMAC_TC1_KEY_LEN];
    uint8_t mac[32];
    char    got[65];

    memset(key, TFWV_HMAC_TC1_KEY_BYTE, sizeof key);
    tfw_hmac_sha256(key, (uint8_t)sizeof key,
                   (const uint8_t *)TFWV_HMAC_TC1_DATA,
                   (uint16_t)strlen(TFWV_HMAC_TC1_DATA), mac);
    hex(mac, 32u, got);
    TEST_ASSERT_EQUAL_STRING(TFWV_HMAC_TC1, got);
}

/* A key longer than the block must be hashed first, per RFC 2104. An
 * implementation that instead truncates it produces a MAC that is wrong and
 * also weaker than it looks. */
static void test_hmac_hashes_an_overlong_key_rather_than_truncating(void)
{
    uint8_t long_key[100];
    uint8_t hashed_key[32];
    uint8_t a[32], b[32];

    for (uint8_t i = 0u; i < sizeof long_key; i++) { long_key[i] = i; }
    tfw_sha256(long_key, sizeof long_key, hashed_key);

    tfw_hmac_sha256(long_key,   (uint8_t)sizeof long_key,  (const uint8_t *)"x", 1u, a);
    tfw_hmac_sha256(hashed_key, (uint8_t)sizeof hashed_key, (const uint8_t *)"x", 1u, b);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 32);
}

/* Different keys must give different MACs over the same data -- the property
 * the whole scheme depends on, stated rather than assumed. */
static void test_hmac_depends_on_the_key(void)
{
    uint8_t k1[16], k2[16], a[32], b[32];

    memset(k1, 0x11, sizeof k1);
    memset(k2, 0x22, sizeof k2);
    tfw_hmac_sha256(k1, (uint8_t)sizeof k1, (const uint8_t *)"same", 4u, a);
    tfw_hmac_sha256(k2, (uint8_t)sizeof k2, (const uint8_t *)"same", 4u, b);

    TEST_ASSERT_FALSE(memcmp(a, b, 32) == 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_of_abc);
    RUN_TEST(test_sha256_of_the_empty_input);
    RUN_TEST(test_sha256_where_the_padding_spills_into_a_second_block);
    RUN_TEST(test_streaming_equals_one_shot);
    RUN_TEST(test_a_single_bit_change_changes_the_digest);
    RUN_TEST(test_hmac_sha256_rfc4231_case_1);
    RUN_TEST(test_hmac_hashes_an_overlong_key_rather_than_truncating);
    RUN_TEST(test_hmac_depends_on_the_key);
    return UNITY_END();
}
