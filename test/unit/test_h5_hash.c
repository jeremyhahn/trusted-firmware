/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/crypto/stm32h5/h5_hash.c (#544) against a model of the STM32H5's HASH
 * peripheral. The model takes the register facts of h5_crypto_regs.h as the
 * peripheral's: it rebuilds the message from the DIN words, the byte swap
 * and STR.NBLW, digests it with the software SHA-256 (src/crypto/sha256.c,
 * the soft provider's), and answers in HR as big-endian words. So a test
 * passes only if the driver configures, feeds and reads the peripheral the
 * way ST's sources say it must, and gets the FIPS 180-2 digests.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "sha256.h"

#define TFW_H5_REG_MODEL 1
#include "../../src/crypto/stm32h5/h5_hash.c"

static struct {
    uint32_t cr;
    uint32_t str;
    uint8_t  msg[65536 + 4];
    uint32_t words;
    uint32_t hr[8];
    int      dcis;
    int      configured;
    int      computed;
    int      hang;          /* never raise DCIS */
    int      stuck;         /* BUSY never clears */
    uint32_t busy_waits;    /* SR reads that asked for BUSY */
    int      fault;         /* an access the peripheral would not expect */
} m;

static uint8_t   regs_anchor[0x400];
#define BASE     ((uintptr_t)regs_anchor)

uint32_t tfw_h5_rd(uintptr_t addr)
{
    uint32_t off = (uint32_t)(addr - BASE);

    if (off == TFW_H5_HASH_SR) {
        m.busy_waits++;
        return (m.stuck ? TFW_H5_HASH_SR_BUSY : 0u) | (m.dcis ? TFW_H5_HASH_SR_DCIS : 0u);
    }
    if (off >= TFW_H5_HASH_HR && off < TFW_H5_HASH_HR + 32u && (off % 4u) == 0u) {
        if (!m.computed)
            m.fault = 1;
        return m.hr[(off - TFW_H5_HASH_HR) / 4u];
    }
    m.fault = 1;
    return 0;
}

static void model_digest(void)
{
    uint32_t nblw = m.str & TFW_H5_HASH_STR_NBLW_MASK;
    uint32_t len  = m.words * 4u;
    uint8_t  d[32];

    if (nblw != 0u) {
        if (m.words == 0u || (nblw % 8u) != 0u) {
            m.fault = 1;
            return;
        }
        len -= 4u - nblw / 8u;
    }
    tfw_sha256(m.msg, (uint16_t)len, d);
    for (uint32_t i = 0; i < 8u; i++)
        m.hr[i] = ((uint32_t)d[4 * i] << 24) | ((uint32_t)d[4 * i + 1] << 16) |
                  ((uint32_t)d[4 * i + 2] << 8) | d[4 * i + 3];
    m.computed = 1;
    m.dcis     = !m.hang;
}

void tfw_h5_wr(uintptr_t addr, uint32_t value)
{
    uint32_t off = (uint32_t)(addr - BASE);

    switch (off) {
    case TFW_H5_HASH_CR:
        /* SHA-256, 8-bit data, a hash (not HMAC), started by INIT. */
        if (value != (TFW_H5_HASH_CR_ALGO_SHA256 | TFW_H5_HASH_CR_BYTE_SWAP |
                      TFW_H5_HASH_CR_INIT))
            m.fault = 1;
        m.cr         = value;
        m.configured = 1;
        m.words      = 0;
        break;
    case TFW_H5_HASH_DIN:
        if (!m.configured || m.computed || m.words * 4u >= sizeof m.msg) {
            m.fault = 1;
            return;
        }
        /* The byte swap: the word's low byte is the message's first. */
        for (uint32_t b = 0; b < 4u; b++)
            m.msg[m.words * 4u + b] = (uint8_t)(value >> (8u * b));
        m.words++;
        break;
    case TFW_H5_HASH_STR:
        if (!m.configured)
            m.fault = 1;
        m.str = value;
        if (value & TFW_H5_HASH_STR_DCAL)
            model_digest();
        break;
    default:
        m.fault = 1;
    }
}

void setUp(void)
{
    memset(&m, 0, sizeof m);
}

void tearDown(void) {}

static void check_against_soft(const uint8_t *data, uint16_t len)
{
    uint8_t want[32], got[32];

    memset(&m, 0, sizeof m);
    tfw_sha256(data, len, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_h5_hash_sha256(BASE, data, len, got));
    TEST_ASSERT_FALSE(m.fault);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

static void check_vector(const char *msg, const uint8_t want[32])
{
    uint8_t got[32];

    TEST_ASSERT_EQUAL(TFW_OK,
                      tfw_h5_hash_sha256(BASE, (const uint8_t *)msg, (uint16_t)strlen(msg), got));
    TEST_ASSERT_FALSE(m.fault);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

static void test_h5_hash_of_the_empty_message(void)
{
    static const uint8_t want[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    check_vector("", want);
    TEST_ASSERT_EQUAL_UINT32(0, m.words);
}

static void test_h5_hash_of_abc(void)
{
    static const uint8_t want[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    check_vector("abc", want);
    TEST_ASSERT_EQUAL_UINT32(24u, m.str & TFW_H5_HASH_STR_NBLW_MASK);
}

static void test_h5_hash_of_the_448_bit_two_block_message(void)
{
    static const uint8_t want[32] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
    };
    check_vector("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", want);
    TEST_ASSERT_EQUAL_UINT32(0u, m.str & TFW_H5_HASH_STR_NBLW_MASK);
}

/* Every length across two blocks and a bit, so each NBLW and each block
 * boundary is crossed, against the software digest. */
static void test_h5_hash_matches_soft_at_every_length_to_140(void)
{
    uint8_t data[140];

    for (uint32_t i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i * 7u + 3u);
    for (uint16_t len = 0; len <= sizeof data; len++)
        check_against_soft(data, len);
}

static void test_h5_hash_matches_soft_at_the_longest_message(void)
{
    static uint8_t data[65535];

    for (uint32_t i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i ^ (i >> 8));
    check_against_soft(data, (uint16_t)sizeof data);
}

/* One BUSY wait per whole block written: 128 bytes are two blocks, so two
 * waits before DCAL, then the DCIS poll's one read. */
static void test_h5_hash_waits_for_busy_after_each_block(void)
{
    uint8_t data[128] = { 0 };

    check_against_soft(data, sizeof data);
    TEST_ASSERT_EQUAL_UINT32(3u, m.busy_waits);
}

static void test_h5_hash_times_out_when_dcis_never_rises(void)
{
    uint8_t out[32];

    memset(out, 0xA5, sizeof out);
    m.hang = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_hash_sha256(BASE, (const uint8_t *)"abc", 3, out));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_POLL_LIMIT, m.busy_waits);
    for (uint32_t i = 0; i < sizeof out; i++)
        TEST_ASSERT_EQUAL_HEX8(0xA5, out[i]);
}

static void test_h5_hash_times_out_when_busy_never_clears(void)
{
    uint8_t data[64] = { 0 }, out[32];

    m.stuck = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_h5_hash_sha256(BASE, data, sizeof data, out));
    TEST_ASSERT_FALSE(m.computed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h5_hash_of_the_empty_message);
    RUN_TEST(test_h5_hash_of_abc);
    RUN_TEST(test_h5_hash_of_the_448_bit_two_block_message);
    RUN_TEST(test_h5_hash_matches_soft_at_every_length_to_140);
    RUN_TEST(test_h5_hash_matches_soft_at_the_longest_message);
    RUN_TEST(test_h5_hash_waits_for_busy_after_each_block);
    RUN_TEST(test_h5_hash_times_out_when_dcis_never_rises);
    RUN_TEST(test_h5_hash_times_out_when_busy_never_clears);
    return UNITY_END();
}
