/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The small SHA-256 (src/crypto/sha256_small.c), the soft tfw-boot's, against
 * the FIPS 180-2 examples and the padding boundaries. It links that file and
 * not sha256.c: both define the API of sha256.h. The digests are literals.
 */
#include <string.h>
#include "unity.h"
#include "sha256.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t TWO_BLOCK[] =
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

static void check(const uint8_t *data, uint16_t len, const uint8_t want[32])
{
    uint8_t out[32];

    tfw_sha256(data, len, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

static void check_a(uint16_t n, const uint8_t want[32])
{
    uint8_t buf[64];

    memset(buf, 'a', sizeof buf);
    check(buf, n, want);
}

static void test_sha256_small_of_the_empty_message(void)
{
    static const uint8_t want[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
        0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    check((const uint8_t *)"", 0u, want);
}

static void test_sha256_small_of_abc(void)
{
    static const uint8_t want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
        0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    check((const uint8_t *)"abc", 3u, want);
}

static const uint8_t TWO_BLOCK_DIGEST[32] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93,
    0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
    0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
};

static void test_sha256_small_of_the_448_bit_two_block_message(void)
{
    check(TWO_BLOCK, 56u, TWO_BLOCK_DIGEST);
}

static void test_sha256_small_of_a_million_a_in_chunks(void)
{
    static const uint8_t want[32] = {
        0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7, 0xe2,
        0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97, 0x20, 0x0e,
        0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0
    };
    uint8_t      chunk[1000];
    uint8_t      out[32];
    tfw_sha256_t c;
    uint16_t     i;

    memset(chunk, 'a', sizeof chunk);
    tfw_sha256_init(&c);
    for (i = 0u; i < 1000u; i++) {
        tfw_sha256_update(&c, chunk, (uint16_t)sizeof chunk);
    }
    tfw_sha256_final(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

/* 55 bytes: the 0x80 and the length fit in the one block. */
static void test_sha256_small_of_55_bytes_fits_one_block(void)
{
    static const uint8_t want[32] = {
        0x9f, 0x43, 0x90, 0xf8, 0xd3, 0x0c, 0x2d, 0xd9, 0x2e, 0xc9, 0xf0, 0x95,
        0xb6, 0x5e, 0x2b, 0x9a, 0xe9, 0xb0, 0xa9, 0x25, 0xa5, 0x25, 0x8e, 0x24,
        0x1c, 0x9f, 0x1e, 0x91, 0x0f, 0x73, 0x43, 0x18
    };
    check_a(55u, want);
}

/* 56 bytes: the length spills into a second block. */
static void test_sha256_small_of_56_bytes_spills_the_length(void)
{
    static const uint8_t want[32] = {
        0xb3, 0x54, 0x39, 0xa4, 0xac, 0x6f, 0x09, 0x48, 0xb6, 0xd6, 0xf9, 0xe3,
        0xc6, 0xaf, 0x0f, 0x5f, 0x59, 0x0c, 0xe2, 0x0f, 0x1b, 0xde, 0x70, 0x90,
        0xef, 0x79, 0x70, 0x68, 0x6e, 0xc6, 0x73, 0x8a
    };
    check_a(56u, want);
}

/* 64 bytes: a whole block, then a padding-only block. */
static void test_sha256_small_of_64_bytes_a_whole_block(void)
{
    static const uint8_t want[32] = {
        0xff, 0xe0, 0x54, 0xfe, 0x7a, 0xe0, 0xcb, 0x6d, 0xc6, 0x5c, 0x3a, 0xf9,
        0xb6, 0x1d, 0x52, 0x09, 0xf4, 0x39, 0x85, 0x1d, 0xb4, 0x3d, 0x0b, 0xa5,
        0x99, 0x73, 0x37, 0xdf, 0x15, 0x46, 0x68, 0xeb
    };
    check_a(64u, want);
}

static void test_sha256_small_split_at_every_offset(void)
{
    uint8_t      out[32];
    tfw_sha256_t c;
    uint16_t     k;

    for (k = 0u; k <= 56u; k++) {
        tfw_sha256_init(&c);
        tfw_sha256_update(&c, TWO_BLOCK, k);
        tfw_sha256_update(&c, &TWO_BLOCK[k], (uint16_t)(56u - k));
        tfw_sha256_final(&c, out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(TWO_BLOCK_DIGEST, out, 32);
    }
}

/* RFC 4231 test cases 1 and 2. */
static void test_hmac_sha256_small_rfc4231_case_1(void)
{
    static const uint8_t want[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce,
        0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    uint8_t key[20];
    uint8_t out[32];

    memset(key, 0x0b, sizeof key);
    tfw_hmac_sha256(key, (uint8_t)sizeof key, (const uint8_t *)"Hi There", 8u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

static void test_hmac_sha256_small_rfc4231_case_2(void)
{
    static const uint8_t want[32] = {
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26,
        0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
        0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
    };
    uint8_t out[32];

    tfw_hmac_sha256((const uint8_t *)"Jefe", 4u,
                    (const uint8_t *)"what do ya want for nothing?", 28u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

/* RFC 4231 case 6: a 131-byte key, longer than the block, which HMAC hashes
 * to 32 bytes before it pads it -- the branch no shorter key reaches. */
static void test_hmac_sha256_small_rfc4231_case_6_hashes_a_long_key(void)
{
    static const uint8_t want[32] = {
        0x60, 0xe4, 0x31, 0x59, 0x1e, 0xe0, 0xb6, 0x7f, 0x0d, 0x8a, 0x26, 0xaa,
        0xcb, 0xf5, 0xb7, 0x7f, 0x8e, 0x0b, 0xc6, 0x21, 0x37, 0x28, 0xc5, 0x14,
        0x05, 0x46, 0x04, 0x0f, 0x0e, 0xe3, 0x7f, 0x54
    };
    static const char data[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    uint8_t key[131];
    uint8_t out[32];

    memset(key, 0xaa, sizeof key);
    tfw_hmac_sha256(key, (uint8_t)sizeof key,
                    (const uint8_t *)data, (uint16_t)(sizeof data - 1u), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

/* RFC 4231 case 7: the same long key over data longer than a block. */
static void test_hmac_sha256_small_rfc4231_case_7_long_key_long_data(void)
{
    static const uint8_t want[32] = {
        0x9b, 0x09, 0xff, 0xa7, 0x1b, 0x94, 0x2f, 0xcb, 0x27, 0x63, 0x5f, 0xbc,
        0xd5, 0xb0, 0xe9, 0x44, 0xbf, 0xdc, 0x63, 0x64, 0x4f, 0x07, 0x13, 0x93,
        0x8a, 0x7f, 0x51, 0x53, 0x5c, 0x3a, 0x35, 0xe2
    };
    static const char data[] =
        "This is a test using a larger than block-size key and a larger "
        "than block-size data. The key needs to be hashed before being "
        "used by the HMAC algorithm.";
    uint8_t key[131];
    uint8_t out[32];

    memset(key, 0xaa, sizeof key);
    tfw_hmac_sha256(key, (uint8_t)sizeof key,
                    (const uint8_t *)data, (uint16_t)(sizeof data - 1u), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_small_of_the_empty_message);
    RUN_TEST(test_sha256_small_of_abc);
    RUN_TEST(test_sha256_small_of_the_448_bit_two_block_message);
    RUN_TEST(test_sha256_small_of_a_million_a_in_chunks);
    RUN_TEST(test_sha256_small_of_55_bytes_fits_one_block);
    RUN_TEST(test_sha256_small_of_56_bytes_spills_the_length);
    RUN_TEST(test_sha256_small_of_64_bytes_a_whole_block);
    RUN_TEST(test_sha256_small_split_at_every_offset);
    RUN_TEST(test_hmac_sha256_small_rfc4231_case_1);
    RUN_TEST(test_hmac_sha256_small_rfc4231_case_2);
    RUN_TEST(test_hmac_sha256_small_rfc4231_case_6_hashes_a_long_key);
    RUN_TEST(test_hmac_sha256_small_rfc4231_case_7_long_key_long_data);
    return UNITY_END();
}
