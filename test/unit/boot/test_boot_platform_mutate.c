/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The platform block, mutated (#163).
 *
 * test_boot_platform defines the reading: a wrong magic is NOT_PROVISIONED, a
 * layout this build does not know is UNSUPPORTED, a CRC that does not match
 * is CRC, and a refusal writes nothing to *out. This holds the parser to that
 * reading for every byte of the block set to every value, for every layout
 * byte under a CRC that matches, and for a seeded run of multi-byte mutations,
 * half of them given a matching CRC so that the accepted path sees arbitrary
 * field values. An accepted block hands back each field from its offset in
 * include/tfw/boot.h, and serialising it into the same bytes changes nothing.
 *
 * Each block is parsed from a heap buffer of exactly TFW_BOOT_EE_BLOCK_LEN bytes, so
 * under `make test-asan` a read past it is a fault. The seed is a literal and
 * every failure prints it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boot_platform.h"
#include "crc16.h"

#define CASES 40000u
#define SEED  0xBB67AE85u

static uint32_t rng;
static char why[80];

void setUp(void) {}
void tearDown(void) {}

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void seal(uint8_t *raw)
{
    uint16_t crc = tfw_crc16(raw, TFW_BOOT_EE_O_CRC);
    raw[TFW_BOOT_EE_O_CRC]      = (uint8_t)crc;
    raw[TFW_BOOT_EE_O_CRC + 1u] = (uint8_t)(crc >> 8);
}

/* The block test_boot_platform builds, with the reserved bytes patterned so
 * a serialiser that rebuilt them would show. */
static void build(uint8_t raw[TFW_BOOT_EE_BLOCK_LEN])
{
    tfw_boot_platform_t p;

    for (uint32_t i = 0; i < TFW_BOOT_EE_BLOCK_LEN; i++) { raw[i] = (uint8_t)(0xC3u ^ i); }
    memset(&p, 0, sizeof p);
    memset(p.pubkey_hash, 0x11, sizeof p.pubkey_hash);
    memset(p.serial, 0x22, sizeof p.serial);
    memset(p.operator_pubkey, 0x33, sizeof p.operator_pubkey);
    p.rollback_floor = 4u;
    p.boot_count     = 17u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    p.fail_count     = 2u;
    tfw_boot_platform_serialise(&p, raw);
}

/* The reading test_boot_platform defines, from the bytes alone. */
static tfw_status_t expected(const uint8_t *raw)
{
    if (rd32(&raw[TFW_BOOT_EE_O_MAGIC]) != TFW_BOOT_EE_MAGIC) { return TFW_ERR_NOT_PROVISIONED; }
    if (raw[TFW_BOOT_EE_O_LAYOUT] != TFW_BOOT_EE_VERSION)      { return TFW_ERR_UNSUPPORTED; }
    if (tfw_crc16(raw, TFW_BOOT_EE_O_CRC) != rd16(&raw[TFW_BOOT_EE_O_CRC])) { return TFW_ERR_CRC; }
    return TFW_OK;
}

/* Parse a heap copy of exactly the block's length and hold the answer to the
 * reading. Returns the status. */
static tfw_status_t check(const uint8_t *src)
{
    uint8_t *raw = malloc(TFW_BOOT_EE_BLOCK_LEN);
    tfw_boot_platform_t out, before;

    TEST_ASSERT_NOT_NULL(raw);
    memcpy(raw, src, TFW_BOOT_EE_BLOCK_LEN);
    memset(&out, 0xA5, sizeof out);
    memcpy(&before, &out, sizeof out);

    tfw_status_t want = expected(raw);
    tfw_status_t rc   = tfw_boot_platform_parse(raw, &out);

    TEST_ASSERT_EQUAL_INT_MESSAGE(want, rc, why);
    if (rc != TFW_OK) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &out, sizeof out, why);
        free(raw);
        return rc;
    }
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&raw[TFW_BOOT_EE_O_PUBKEY_HASH], out.pubkey_hash,
                                     TFW_BOOT_MEASURE_LEN, why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rd32(&raw[TFW_BOOT_EE_O_ROLLBACK_FLOOR]), out.rollback_floor, why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rd32(&raw[TFW_BOOT_EE_O_BOOT_COUNT]), out.boot_count, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(raw[TFW_BOOT_EE_O_BOOT_STATE], out.boot_state, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(raw[TFW_BOOT_EE_O_OTA_STATE], out.ota_state, why);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(rd16(&raw[TFW_BOOT_EE_O_FAIL_COUNT]), out.fail_count, why);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&raw[TFW_BOOT_EE_O_SERIAL], out.serial, TFW_BOOT_SERIAL_LEN, why);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&raw[TFW_BOOT_EE_O_OPERATOR_PUBKEY], out.operator_pubkey,
                                     TFW_BOOT_OPERATOR_PUBKEY_LEN, why);

    /* Written back into its own bytes, an accepted block is unchanged: the
     * flags and the reserved span survive, and the CRC is the one it had. */
    uint8_t *again = malloc(TFW_BOOT_EE_BLOCK_LEN);
    TEST_ASSERT_NOT_NULL(again);
    memcpy(again, raw, TFW_BOOT_EE_BLOCK_LEN);
    tfw_boot_platform_serialise(&out, again);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(raw, again, TFW_BOOT_EE_BLOCK_LEN, why);
    free(again);
    free(raw);
    return rc;
}

static void test_every_byte_set_to_every_value(void)
{
    uint8_t good[TFW_BOOT_EE_BLOCK_LEN], raw[TFW_BOOT_EE_BLOCK_LEN];
    uint32_t seen[4] = { 0 };

    build(good);
    for (uint32_t at = 0; at < TFW_BOOT_EE_BLOCK_LEN; at++) {
        for (uint32_t v = 0; v < 256u; v++) {
            memcpy(raw, good, sizeof raw);
            raw[at] = (uint8_t)v;
            snprintf(why, sizeof why, "byte %u value 0x%02X", at, v);
            tfw_status_t rc = check(raw);
            /* One byte changed is never accepted, unless it is a byte the
             * CRC does not cover or the byte it already held. */
            if (v != good[at] && at < TFW_BOOT_EE_O_CRC + 2u) {
                TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, rc, why);
            }
            seen[rc == TFW_OK ? 0 : rc == TFW_ERR_NOT_PROVISIONED ? 1 : rc == TFW_ERR_UNSUPPORTED ? 2 : 3]++;
        }
    }
    for (uint32_t k = 0; k < 4u; k++) {
        TEST_ASSERT_TRUE(seen[k] > 0u);
    }
}

static void test_every_layout_under_a_matching_crc(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];

    for (uint32_t v = 0; v < 256u; v++) {
        build(raw);
        raw[TFW_BOOT_EE_O_LAYOUT] = (uint8_t)v;
        seal(raw);
        snprintf(why, sizeof why, "layout %u sealed", v);
        TEST_ASSERT_EQUAL_INT_MESSAGE(v == TFW_BOOT_EE_VERSION ? TFW_OK : TFW_ERR_UNSUPPORTED,
                                      check(raw), why);
    }
}

static void test_a_blank_or_zeroed_block_reads_not_provisioned(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];

    for (uint32_t fill = 0; fill < 256u; fill++) {
        memset(raw, (int)fill, sizeof raw);
        snprintf(why, sizeof why, "fill 0x%02X", fill);
        TEST_ASSERT_EQUAL_INT_MESSAGE(TFW_ERR_NOT_PROVISIONED, check(raw), why);
    }
}

static void test_seeded_multi_byte_mutations(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];
    uint32_t accepted = 0;

    for (uint32_t c = 0; c < CASES; c++) {
        rng = SEED ^ (c * 0x9E3779B9u);
        if (rng == 0u) { rng = SEED; }
        build(raw);
        for (uint32_t k = 1u + next() % 8u; k > 0u; k--) {
            uint32_t at = next() % TFW_BOOT_EE_BLOCK_LEN;
            switch (next() % 3u) {
            case 0:  raw[at] = (uint8_t)next(); break;
            case 1:  raw[at] ^= (uint8_t)(1u << (next() % 8u)); break;
            default: memset(&raw[at], 0xFF, TFW_BOOT_EE_BLOCK_LEN - at); break;
            }
        }
        if (next() % 2u == 0u) {
            seal(raw);
        }
        snprintf(why, sizeof why, "seed 0x%08X case %u", SEED, c);
        accepted += (check(raw) == TFW_OK) ? 1u : 0u;
    }
    TEST_ASSERT_TRUE(accepted > CASES / 10u);
    TEST_ASSERT_TRUE(accepted < CASES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_byte_set_to_every_value);
    RUN_TEST(test_every_layout_under_a_matching_crc);
    RUN_TEST(test_a_blank_or_zeroed_block_reads_not_provisioned);
    RUN_TEST(test_seeded_multi_byte_mutations);
    return UNITY_END();
}
