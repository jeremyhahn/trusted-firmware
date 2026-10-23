/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/esp32/l0_crypto.c, the SHA-256 src/core/suit.c asks tfw/crypto.h
 * for in layer 0 (#48, #163), alone: against measure_soft.c's hash here and,
 * as test_boot_l0_crypto_mutate_sha, against measure_esp32.c's over the
 * bootloader SHA mock, so each link the bootloader can take is pinned.
 *
 * The mutants this kills: the hash call skipped, an error returned, the
 * length or the source moved by one, the length narrowed to a byte, the
 * digest written past or short of its 32 bytes. The digests are FIPS 180-2's
 * published vectors, literals, never computed by the code under test.
 */
#include <string.h>

#include "unity.h"
#include "tfw/crypto.h"
#include "boot_measure.h"

#define GUARD 0xA5u

/* FIPS 180-2 B.1, B.2 and the empty message. */
static const uint8_t SHA_EMPTY[TFW_SHA256_LEN] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
};
static const uint8_t SHA_ABC[TFW_SHA256_LEN] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};
static const char    MSG_448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const uint8_t SHA_448[TFW_SHA256_LEN] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
};

/* The digest with a guard byte either side: a write one short of, or one past,
 * its 32 bytes shows. */
static uint8_t box[1 + TFW_SHA256_LEN + 1];

void setUp(void)
{
    memset(box, GUARD, sizeof box);
}

void tearDown(void) {}

static void assert_guards(void)
{
    TEST_ASSERT_EQUAL_HEX8(GUARD, box[0]);
    TEST_ASSERT_EQUAL_HEX8(GUARD, box[1 + TFW_SHA256_LEN]);
}

static void test_the_empty_message_is_the_published_digest(void)
{
    static const uint8_t none[1] = { 0x61u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(none, 0u, &box[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_EMPTY, &box[1], TFW_SHA256_LEN);
    assert_guards();
}

static void test_abc_is_the_published_digest(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abc", 3u, &box[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, &box[1], TFW_SHA256_LEN);
    assert_guards();
}

/* Only the first len bytes: the byte after them is not hashed, and the first
 * is. */
static void test_only_the_bytes_named_are_hashed(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"abcd", 3u, &box[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_ABC, &box[1], TFW_SHA256_LEN);
    memset(box, GUARD, sizeof box);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)"xabc", 3u, &box[1]));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(SHA_ABC, &box[1], TFW_SHA256_LEN));
    assert_guards();
}

/* Two blocks of padding: 56 bytes. */
static void test_the_two_block_message_is_the_published_digest(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256((const uint8_t *)MSG_448,
                                                (uint16_t)(sizeof MSG_448 - 1u), &box[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SHA_448, &box[1], TFW_SHA256_LEN);
    assert_guards();
}

/* A length past a byte's range, as a SUIT manifest is: the whole of it, not
 * its low eight bits. Against the boot's own hash of the same span. */
static void test_a_length_past_255_is_hashed_whole(void)
{
    static uint8_t msg[300];
    uint8_t        want[TFW_SHA256_LEN];
    uint8_t        low[TFW_SHA256_LEN];
    uint16_t       i;

    for (i = 0u; i < sizeof msg; i++) {
        msg[i] = (uint8_t)(i * 31u + 7u);
    }
    tfw_boot_hash(msg, (uint16_t)sizeof msg, want);
    tfw_boot_hash(msg, (uint16_t)(sizeof msg & 0xFFu), low);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(want, low, sizeof want));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(msg, (uint16_t)sizeof msg, &box[1]));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &box[1], TFW_SHA256_LEN);
    assert_guards();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_empty_message_is_the_published_digest);
    RUN_TEST(test_abc_is_the_published_digest);
    RUN_TEST(test_only_the_bytes_named_are_hashed);
    RUN_TEST(test_the_two_block_message_is_the_published_digest);
    RUN_TEST(test_a_length_past_255_is_hashed_whole);
    return UNITY_END();
}
