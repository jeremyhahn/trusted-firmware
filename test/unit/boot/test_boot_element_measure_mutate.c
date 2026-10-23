/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The element-backed measure, boot/measure/measure_atecc608a.c, against seeded
 * mutants (#163), on the software element as test_boot_element is. A reader
 * owned by the test logs every address and length it is asked for and fails on
 * the call it is told to, so a measurement's walk over the image is pinned:
 * base + 0, + 64, ... and a tail that ends at len, never a read past it. The
 * element is failed after its Nth command, so a refusal mid-hash is an error
 * and no digest, and the element is asleep after every path.
 *
 * Mutants tried, each killed by the test named:
 *   hash: out not zeroed first                 -> a_hash_refused_midway_is_zero
 *   hash: `> SHA_BLOCK` -> `>=`                -> the_hash_sends_whole_blocks_then_the_tail
 *   hash: offset step 63                       -> the_hash_sends_whole_blocks_then_the_tail
 *   hash: loop ignores a refused command       -> a_dropped_start_sends_nothing_more
 *   hash: sleep skipped                        -> a_hash_refused_midway_is_zero,
 *                                                 an_absent_element_hashes_to_zero_and_sleeps
 *   measure: `len == 0` refusal dropped        -> the_length_bounds
 *   measure: `> MAX` -> `>=`, bound dropped    -> the_length_bounds
 *   measure: absent element returns RANGE/OK   -> an_absent_element_refuses_to_measure
 *   measure: `> SHA_BLOCK` -> `>=`             -> the_walk_is_base_plus_block_then_tail
 *   measure: base ignored, addr step 63        -> the_walk_is_base_plus_block_then_tail
 *   measure: tail one short                    -> the_walk_is_base_plus_block_then_tail
 *   measure: block read failure ignored        -> a_reader_failure_is_the_result
 *   measure: tail read failure ignored         -> a_reader_failure_is_the_result
 *   measure: Update refusal ignored            -> an_element_refusal_midway_is_the_result
 *   measure: sleep skipped                     -> every_measure_path_sleeps
 *   tfw_boot_measure from another base         -> the_flash_measure_reads_the_app_region
 * Survive, and equivalent from outside on this model (28 tried, 25 killed):
 *   hash: wake refusal falls through (measure_atecc608a.c:50) and End sent
 *   after a refusal (:60): a part that did not wake, or that dropped a
 *   command, answers nothing more, so the digest stays the zero it was set to.
 *   measure: the block scrub dropped (:120): stack contents after return.
 */
#include <string.h>

#include "unity.h"
#include "boot_measure.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "sha256.h"

#define BASE 0x1000u
#define MAX_READS 600u

static uint32_t g_addr[MAX_READS];
static uint8_t  g_len[MAX_READS];
static uint16_t g_reads;
static uint16_t g_fail_at;          /* 1-based call that fails; 0 never */

static uint8_t pattern(uint32_t a) { return (uint8_t)(a * 7u + 3u); }

static tfw_status_t reader(uint32_t addr, uint8_t *buf, uint8_t len)
{
    if (g_reads < MAX_READS) {
        g_addr[g_reads] = addr;
        g_len[g_reads]  = len;
    }
    g_reads++;
    if (g_reads == g_fail_at) {
        return TFW_ERR_BUSY;
    }
    for (uint8_t i = 0u; i < len; i++) { buf[i] = pattern(addr + i - BASE); }
    return TFW_OK;
}

static void want_digest(uint32_t len, uint8_t out[32])
{
    static uint8_t img[TFW_BOOT_MAX_IMAGE_LEN];

    for (uint32_t i = 0u; i < len; i++) { img[i] = pattern(i); }
    tfw_sha256(img, (uint16_t)len, out);
}

void setUp(void)
{
    tfw_model_reset();
    tfw_boot_mock_reset();
    g_reads = 0u;
    g_fail_at = 0u;
}

void tearDown(void) {}

/* ------------------------------------------------------------ tfw_boot_hash */

static void test_the_hash_sends_whole_blocks_then_the_tail(void)
{
    static const uint16_t lens[] = { 0u, 1u, 63u, 64u, 65u, 128u, 129u, 200u };
    uint8_t data[200], want[32], got[32];

    for (uint8_t i = 0u; i < sizeof data; i++) { data[i] = pattern(i); }
    for (uint8_t k = 0u; k < sizeof lens / sizeof lens[0]; k++) {
        uint16_t n = lens[k];
        uint8_t  updates = (uint8_t)(n > 64u ? (n - 1u) / 64u : 0u);
        const tfw_model_cmd_t *end;

        tfw_model_reset();
        tfw_sha256(data, n, want);
        tfw_boot_hash(data, n, got);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
        TEST_ASSERT_EQUAL_UINT8(2u + updates, tfw_model_log_count());
        for (uint8_t u = 0u; u < updates; u++) {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(&data[u * 64u], tfw_model_log((uint8_t)(1u + u))->data, 64);
        }
        end = tfw_model_log((uint8_t)(1u + updates));
        TEST_ASSERT_EQUAL_HEX8(0x02u, end->p1);
        TEST_ASSERT_EQUAL_UINT8(n - updates * 64u, end->dlen);
        TEST_ASSERT_FALSE(tfw_model_is_awake());
    }
}

/* Start accepted, the first Update refused: no End, a zero digest, asleep. */
static void test_a_hash_refused_midway_is_zero(void)
{
    uint8_t data[200] = {1}, zero[32] = {0}, got[32];

    tfw_model_fail_commands_after(1u);
    memset(got, 0xA5, sizeof got);
    tfw_boot_hash(data, (uint16_t)sizeof data, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, got, 32);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

static void test_an_absent_element_hashes_to_zero_and_sleeps(void)
{
    uint8_t zero[32] = {0}, got[32];

    tfw_model_set_absent(1u);
    memset(got, 0xA5, sizeof got);
    tfw_boot_hash((const uint8_t *)"abc", 3u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, got, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

/* A Start the part dropped, once: nothing after it is sent, the digest is
 * zero, though the part would take the next command. */
static void test_a_dropped_start_sends_nothing_more(void)
{
    uint8_t data[200] = {1}, zero[32] = {0}, got[32];

    tfw_model_nack_next_command();
    memset(got, 0xA5, sizeof got);
    tfw_boot_hash(data, (uint16_t)sizeof data, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, got, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* A wake that fails on a part that would answer: no command is sent. */
static void test_a_failed_wake_sends_no_command(void)
{
    uint8_t zero[32] = {0}, got[32];

    tfw_model_fail_wakes_after(0u);
    memset(got, 0xA5, sizeof got);
    tfw_boot_hash((const uint8_t *)"abc", 3u, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, got, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* --------------------------------------------------- tfw_boot_measure_from */

static void test_the_length_bounds(void)
{
    uint8_t want[32], got[32];

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(reader, BASE, 0u, got));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_boot_measure_from(reader, BASE, TFW_BOOT_MAX_IMAGE_LEN + 1u, got));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure_from(reader, BASE, 0xFFFFFFFFu, got));
    TEST_ASSERT_EQUAL_UINT16(0u, g_reads);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());

    want_digest(TFW_BOOT_MAX_IMAGE_LEN, want);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, BASE, TFW_BOOT_MAX_IMAGE_LEN, got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

static void test_an_absent_element_refuses_to_measure(void)
{
    uint8_t got[32];

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_measure_from(reader, BASE, 100u, got));
    TEST_ASSERT_EQUAL_UINT16(0u, g_reads);
}

static void test_the_walk_is_base_plus_block_then_tail(void)
{
    static const uint32_t lens[] = { 1u, 63u, 64u, 65u, 128u, 129u, 200u };
    uint8_t want[32], got[32];

    for (uint8_t k = 0u; k < sizeof lens / sizeof lens[0]; k++) {
        uint32_t n = lens[k];
        uint16_t blocks = (uint16_t)((n - 1u) / 64u);

        setUp();
        want_digest(n, want);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, BASE, n, got));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
        TEST_ASSERT_EQUAL_UINT16(blocks + 1u, g_reads);
        for (uint16_t r = 0u; r < g_reads; r++) {
            TEST_ASSERT_EQUAL_HEX32(BASE + r * 64u, g_addr[r]);
            TEST_ASSERT_EQUAL_UINT8(r < blocks ? 64u : (uint8_t)(n - blocks * 64u), g_len[r]);
        }
        TEST_ASSERT_EQUAL_UINT8(2u + blocks, tfw_model_log_count());
    }
}

/* A reader failure, on a whole block or on the tail, is the result. */
static void test_a_reader_failure_is_the_result(void)
{
    uint8_t got[32];

    g_fail_at = 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_boot_measure_from(reader, BASE, 200u, got));
    TEST_ASSERT_EQUAL_UINT16(1u, g_reads);

    setUp();
    g_fail_at = 4u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_boot_measure_from(reader, BASE, 200u, got));
    TEST_ASSERT_EQUAL_UINT16(4u, g_reads);
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_model_log_count());    /* Start, three Updates, no End */
}

static void test_an_element_refusal_midway_is_the_result(void)
{
    uint8_t got[32];

    tfw_model_fail_commands_after(2u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, BASE, 200u, got));
    TEST_ASSERT_EQUAL_UINT16(2u, g_reads);
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_model_log_count());
}

static void test_every_measure_path_sleeps(void)
{
    uint8_t got[32];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure_from(reader, BASE, 100u, got));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    setUp();
    g_fail_at = 2u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BUSY, tfw_boot_measure_from(reader, BASE, 100u, got));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    setUp();
    tfw_model_fail_commands_after(1u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, BASE, 100u, got));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

/* tfw_boot_measure walks the flash from the application region's base. */
static void test_the_flash_measure_reads_the_app_region(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  want[32], got[32];

    for (uint16_t i = 0u; i < 300u; i++) { flash[TFW_BOOT_APP_BASE + i] = pattern(i); }
    flash[TFW_BOOT_APP_BASE + 300u] = 0x5Au;
    want_digest(300u, want);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(300u, got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_hash_sends_whole_blocks_then_the_tail);
    RUN_TEST(test_a_hash_refused_midway_is_zero);
    RUN_TEST(test_an_absent_element_hashes_to_zero_and_sleeps);
    RUN_TEST(test_a_dropped_start_sends_nothing_more);
    RUN_TEST(test_a_failed_wake_sends_no_command);
    RUN_TEST(test_the_length_bounds);
    RUN_TEST(test_an_absent_element_refuses_to_measure);
    RUN_TEST(test_the_walk_is_base_plus_block_then_tail);
    RUN_TEST(test_a_reader_failure_is_the_result);
    RUN_TEST(test_an_element_refusal_midway_is_the_result);
    RUN_TEST(test_every_measure_path_sleeps);
    RUN_TEST(test_the_flash_measure_reads_the_app_region);
    return UNITY_END();
}
