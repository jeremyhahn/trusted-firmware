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

/* FIPS 180-4's own examples beyond one block, literals here because they pin
 * the hash and not the wire. Run against sha256.c and sha256_small.c alike
 * (mk/boot.mk's test_sha256_small), so the two cannot disagree (#130). */
static void test_sha256_of_the_896_bit_two_block_message(void)
{
    static const char msg[] =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
        "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    expect((const uint8_t *)msg, (uint16_t)strlen(msg),
           "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

static void test_sha256_of_a_million_a(void)
{
    uint8_t      chunk[1000];
    uint8_t      d[32];
    char         got[65];
    tfw_sha256_t c;
    uint16_t     i;

    memset(chunk, 'a', sizeof chunk);
    tfw_sha256_init(&c);
    for (i = 0u; i < 1000u; i++) {
        tfw_sha256_update(&c, chunk, (uint16_t)sizeof chunk);
    }
    tfw_sha256_final(&c, d);
    hex(d, 32u, got);
    TEST_ASSERT_EQUAL_STRING(
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", got);
}

/* RFC 4231 section 4.3, test case 2: a key shorter than the block. */
static void test_hmac_sha256_rfc4231_case_2(void)
{
    static const char data[] = "what do ya want for nothing?";
    uint8_t mac[32];
    char    got[65];

    tfw_hmac_sha256((const uint8_t *)"Jefe", 4u, (const uint8_t *)data,
                   (uint16_t)strlen(data), mac);
    hex(mac, 32u, got);
    TEST_ASSERT_EQUAL_STRING(
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", got);
}

/* 55 bytes leave exactly room for the 0x80 and the length: the padding must
 * not spill into a second block. Digest from coreutils sha256sum. */
static void test_sha256_of_55_bytes_keeps_the_length_in_one_block(void)
{
    uint8_t m[55];

    memset(m, 'a', sizeof m);
    expect(m, (uint16_t)sizeof m,
           "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
}

/* A key of exactly 64 bytes is used as it is, zero-padded like any short key,
 * and not hashed first: so it agrees with its own first 63 bytes when the last
 * is zero, and differs from the HMAC under its hash. */
static void test_hmac_takes_a_64_byte_key_as_it_is(void)
{
    uint8_t key[64];
    uint8_t hashed_key[32];
    uint8_t a[32], b[32], c[32];

    for (uint8_t i = 0u; i < sizeof key; i++) { key[i] = (uint8_t)(i + 1u); }
    key[63] = 0u;
    tfw_sha256(key, sizeof key, hashed_key);

    tfw_hmac_sha256(key,        64u, (const uint8_t *)"x", 1u, a);
    tfw_hmac_sha256(key,        63u, (const uint8_t *)"x", 1u, b);
    tfw_hmac_sha256(hashed_key, 32u, (const uint8_t *)"x", 1u, c);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, a, 32);
    TEST_ASSERT_TRUE(memcmp(a, c, 32) != 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_of_abc);
    RUN_TEST(test_sha256_of_the_empty_input);
    RUN_TEST(test_sha256_where_the_padding_spills_into_a_second_block);
    RUN_TEST(test_hmac_sha256_rfc4231_case_1);
    RUN_TEST(test_hmac_hashes_an_overlong_key_rather_than_truncating);
    RUN_TEST(test_sha256_of_the_896_bit_two_block_message);
    RUN_TEST(test_sha256_of_a_million_a);
    RUN_TEST(test_hmac_sha256_rfc4231_case_2);
    RUN_TEST(test_sha256_of_55_bytes_keeps_the_length_in_one_block);
    RUN_TEST(test_hmac_takes_a_64_byte_key_as_it_is);
    return UNITY_END();
}
