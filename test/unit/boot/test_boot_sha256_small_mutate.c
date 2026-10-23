/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The faults test_sha256_small let through, planted in src/crypto/sha256_small.c
 * (#163): the HMAC key's 64-byte bound (`keylen > 64u`) and the length's high
 * word (`n >> 29`), which only a message of 2^29 bytes or more sets. That
 * message is fed from one zeroed buffer, never allocated whole. It lives under
 * test/unit/boot/ so the host's test/unit/test_*.c wildcard, which links
 * sha256.c, never picks it up. The digests are literals.
 */
#include <string.h>
#include "unity.h"
#include "sha256.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t HI_THERE[8] = { 'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e' };

static void check_hmac(uint8_t keylen, const uint8_t want[32])
{
    uint8_t key[65];
    uint8_t out[32];
    uint8_t i;

    for (i = 0u; i < keylen; i++) {
        key[i] = i;
    }
    tfw_hmac_sha256(key, keylen, HI_THERE, sizeof HI_THERE, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

/* 64 bytes is one block: used as it is, not hashed. Kills `keylen >= 64u`. */
static void test_hmac_a_64_byte_key_is_not_hashed(void)
{
    static const uint8_t want[32] = {
        0xe3, 0x11, 0x76, 0x9a, 0x0a, 0x9a, 0x3a, 0xf1, 0xad, 0x9d, 0xa7, 0x4c,
        0x19, 0x33, 0xba, 0xb5, 0xac, 0x0a, 0xa4, 0x83, 0x67, 0xb5, 0x5a, 0xb6,
        0xec, 0x99, 0x55, 0x08, 0xbd, 0xab, 0x1d, 0xb6
    };

    check_hmac(64u, want);
}

/* 65 bytes is the first length hashed first. Kills `keylen > 65u`. */
static void test_hmac_a_65_byte_key_is_hashed(void)
{
    static const uint8_t want[32] = {
        0x6c, 0xae, 0x15, 0x09, 0x76, 0x5e, 0xf0, 0x78, 0xac, 0xe5, 0x06, 0x9d,
        0xe9, 0x72, 0x13, 0xef, 0x2c, 0x56, 0xa7, 0x8d, 0x52, 0x2d, 0x68, 0xd8,
        0xad, 0xdc, 0x50, 0x07, 0x74, 0x0e, 0x96, 0x4b
    };

    check_hmac(65u, want);
}

/* 63 bytes, zero-padded to the block: the pad bytes past the key are zero. */
static void test_hmac_a_63_byte_key_is_zero_padded(void)
{
    static const uint8_t want[32] = {
        0x15, 0x11, 0x4f, 0x28, 0x65, 0x83, 0xa7, 0x44, 0xe6, 0x50, 0xa1, 0xfd,
        0xd6, 0x29, 0x05, 0x27, 0x8d, 0x78, 0x19, 0x40, 0xe8, 0x87, 0x56, 0x4a,
        0xc8, 0x77, 0xbb, 0x7e, 0xbf, 0xa6, 0xe5, 0x8f
    };

    check_hmac(63u, want);
}

/* 2^29 zero bytes, fed 32 KB at a time from one buffer. The context is
 * copied there, so one feed (the slow part) serves both lengths. */
static uint8_t zeros[32768];

/* The bit length is 2^32: its high word is 1, its low word 0. Kills a high
 * word dropped, `n >> 28` and `n >> 30`. Then three bytes more, still in the
 * buffer at the end, so the count of whole blocks and buflen both carry. */
static void test_sha256_small_of_2_pow_29_bytes_sets_the_high_word(void)
{
    static const uint8_t want[32] = {
        0x9a, 0xcc, 0xa8, 0xe8, 0xc2, 0x22, 0x01, 0x15, 0x53, 0x89, 0xf6, 0x5a,
        0xbb, 0xf6, 0xbc, 0x97, 0x23, 0xed, 0xc7, 0x38, 0x4e, 0xad, 0x80, 0x50,
        0x38, 0x39, 0xf4, 0x9d, 0xcc, 0x56, 0xd7, 0x67
    };
    static const uint8_t want_plus_3[32] = {
        0x71, 0x7d, 0xbb, 0x38, 0xe9, 0x02, 0xc8, 0xcc, 0xfe, 0xc5, 0x8b, 0xd3,
        0xad, 0x0c, 0x34, 0x91, 0x3f, 0xab, 0x48, 0x50, 0xd8, 0x69, 0x92, 0xad,
        0x32, 0x6e, 0xd3, 0xad, 0xa0, 0x38, 0x26, 0x53
    };
    tfw_sha256_t c, at_2_pow_29;
    uint8_t      out[32];
    uint32_t     done;

    tfw_sha256_init(&c);
    for (done = 0u; done < (1ul << 29); done += sizeof zeros) {
        tfw_sha256_update(&c, zeros, (uint16_t)sizeof zeros);
    }
    at_2_pow_29 = c;

    tfw_sha256_final(&at_2_pow_29, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);

    tfw_sha256_update(&c, (const uint8_t *)"abc", 3u);
    tfw_sha256_final(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_plus_3, out, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hmac_a_64_byte_key_is_not_hashed);
    RUN_TEST(test_hmac_a_65_byte_key_is_hashed);
    RUN_TEST(test_hmac_a_63_byte_key_is_zero_padded);
    RUN_TEST(test_sha256_small_of_2_pow_29_bytes_sets_the_high_word);
    return UNITY_END();
}
