/*
 * Mutation tests for the element measurement, measure_atecc608a.c (#163).
 *
 * test_boot_element shows the digests are right. This file pins what reaches
 * the element and the reader on the way: the length bounds, the block split,
 * the address each read is given, that a refusal anywhere ends the sequence
 * and is the answer, and that the part is put back to sleep on every path.
 * The element is the model; the reader is a stub over a local buffer.
 */
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "atecc.h"
#include "sha256.h"

#define BLK 64u

static uint8_t  g_src[600];
static uint32_t g_base;
static uint32_t g_addr[16];
static uint8_t  g_len[16];
static uint8_t  g_reads;
static uint8_t  g_fail_at;   /* 1-based read that fails; 0 = none */

static tfw_status_t reader(uint32_t addr, uint8_t *buf, uint8_t len)
{
    if (g_reads < 16u) { g_addr[g_reads] = addr; g_len[g_reads] = len; }
    g_reads++;
    if (g_fail_at != 0u && g_reads == g_fail_at) {
        return TFW_ERR_TIMEOUT;
    }
    memcpy(buf, &g_src[addr - g_base], len);
    return TFW_OK;
}

void setUp(void)
{
    uint16_t i;

    tfw_model_reset();
    tfw_boot_mock_reset();
    for (i = 0u; i < sizeof g_src; i++) { g_src[i] = (uint8_t)(i * 13u + 5u); }
    g_base = 0x1000u; g_reads = 0u; g_fail_at = 0u;
    memset(g_addr, 0, sizeof g_addr);
    memset(g_len, 0, sizeof g_len);
}
void tearDown(void) {}

/* The SHA command at log index i: its mode, its param2 and its data length. */
static void check_sha(uint8_t i, uint8_t mode, uint16_t p2, uint8_t dlen)
{
    const tfw_model_cmd_t *c = tfw_model_log(i);

    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_SHA, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(mode, c->p1);
    TEST_ASSERT_EQUAL_UINT16(p2, c->p2);
    TEST_ASSERT_EQUAL_UINT8(dlen, c->dlen);
}

/* ------------------------------------------------------ tfw_boot_measure_from */

/* Kills: the zero check dropped or flipped, the upper bound moved by one,
 * either check after the wake. */
static void test_measure_takes_one_to_max_bytes(void)
{
    uint8_t out[32];

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_measure_from(reader, g_base, 0u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        tfw_boot_measure_from(reader, g_base, TFW_BOOT_MAX_IMAGE_LEN + 1u, out));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(0u, g_reads);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    /* The largest image is taken: the first read is where it stops. */
    g_fail_at = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        tfw_boot_measure_from(reader, g_base, TFW_BOOT_MAX_IMAGE_LEN, out));
    TEST_ASSERT_EQUAL_UINT8(1u, g_reads);

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, g_base, 1u, out));
    TEST_ASSERT_EQUAL_UINT8(1u, g_reads);
}

/* Kills: a measurement with no element returning a zero digest as OK. */
static void test_measure_with_no_element_is_a_crypto_error(void)
{
    uint8_t out[32];

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_measure_from(reader, g_base, 100u, out));
    TEST_ASSERT_EQUAL_UINT8(0u, g_reads);
}

/* Kills: the block loop bound moved (> to >=, a block of 63 or 65), the read
 * address not offset by base or not advanced, the tail length wrong. Each
 * length is checked read by read, command by command, and digest. */
static void test_measure_splits_into_blocks_and_a_tail(void)
{
    static const uint16_t lens[] = { 1u, 63u, 64u, 65u, 128u, 129u, 200u };
    uint8_t k;

    for (k = 0u; k < sizeof lens / sizeof lens[0]; k++) {
        uint16_t len = lens[k];
        uint8_t  whole = (uint8_t)((len - 1u) / BLK);
        uint8_t  tail = (uint8_t)(len - whole * BLK);
        uint8_t  want[32], got[32];
        uint8_t  i;

        setUp();
        tfw_sha256(g_src, len, want);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, g_base, len, got));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);

        TEST_ASSERT_EQUAL_UINT8(whole + 1u, g_reads);
        for (i = 0u; i < whole; i++) {
            TEST_ASSERT_EQUAL_UINT32(g_base + i * BLK, g_addr[i]);
            TEST_ASSERT_EQUAL_UINT8(BLK, g_len[i]);
        }
        TEST_ASSERT_EQUAL_UINT32(g_base + whole * BLK, g_addr[whole]);
        TEST_ASSERT_EQUAL_UINT8(tail, g_len[whole]);

        TEST_ASSERT_EQUAL_UINT8(whole + 2u, tfw_model_log_count());
        check_sha(0u, TFW_ATECC_SHA_SHA256_START, 0u, 0u);
        for (i = 0u; i < whole; i++) {
            check_sha((uint8_t)(1u + i), TFW_ATECC_SHA_SHA256_UPDATE, BLK, BLK);
        }
        check_sha((uint8_t)(1u + whole), TFW_ATECC_SHA_SHA256_END, tail, tail);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
}

/* Kills: a reader failure ignored in the loop or at the tail, the element
 * driven after it, the failure not being the answer, sleep skipped. */
static void test_a_failed_read_ends_the_measurement(void)
{
    uint8_t at;

    for (at = 1u; at <= 4u; at++) {
        uint8_t out[32];

        setUp();
        g_fail_at = at;
        TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_boot_measure_from(reader, g_base, 200u, out));
        TEST_ASSERT_EQUAL_UINT8(at, g_reads);
        /* START and one UPDATE per read that worked; nothing after. */
        TEST_ASSERT_EQUAL_UINT8(at, tfw_model_log_count());
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
}

/* Kills: an element refusal ignored (START, an UPDATE or END), the reads
 * carried on after it, the refusal not being the answer, sleep skipped. */
static void test_an_element_refusal_ends_the_measurement(void)
{
    uint8_t n;

    for (n = 0u; n < 5u; n++) {
        uint8_t out[32];

        setUp();
        tfw_model_fail_commands_after(n);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, g_base, 200u, out));
        TEST_ASSERT_EQUAL_UINT8(n, tfw_model_log_count());
        TEST_ASSERT_EQUAL_UINT8(n, g_reads);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
    /* A timed-out UPDATE alone: the next read is never asked for. */
    setUp();
    {
        uint8_t out[32];

        tfw_model_set_exec_ms(TFW_ATECC_OP_SHA, 1000u);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, g_base, 200u, out));
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
        TEST_ASSERT_EQUAL_UINT8(0u, g_reads);
    }
}

/* Kills: tfw_boot_measure reading from anywhere but the application base, or
 * measuring a length other than the one it was given. */
static void test_measure_reads_program_memory_from_the_app_base(void)
{
    uint8_t want[32], got[32];

    memcpy(&tfw_boot_mock_flash()[TFW_BOOT_APP_BASE], g_src, 130u);
    tfw_sha256(g_src, 130u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure(130u, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_measure(0u, got));
}

/* ------------------------------------------------------------ tfw_boot_hash */

/* Kills: the zeroing dropped, so a hash with no element leaves old bytes. */
static void test_hash_with_no_element_is_all_zero(void)
{
    static const uint8_t zero[32];
    uint8_t out[32];

    memset(out, 0xAAu, sizeof out);
    tfw_model_set_absent(1u);
    tfw_boot_hash(g_src, 100u, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, out, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* Kills: the hash block loop bound moved, the data not advanced, the tail
 * length wrong, an empty tail sent with data. */
static void test_hash_splits_into_blocks_and_a_tail(void)
{
    static const uint16_t lens[] = { 0u, 1u, 32u, 64u, 65u, 128u, 129u, 300u };
    uint8_t k;

    for (k = 0u; k < sizeof lens / sizeof lens[0]; k++) {
        uint16_t len = lens[k];
        uint8_t  whole = (uint8_t)(len ? (len - 1u) / BLK : 0u);
        uint8_t  tail = (uint8_t)(len - whole * BLK);
        uint8_t  want[32], got[32];
        uint8_t  i;

        setUp();
        tfw_sha256(g_src, len, want);
        tfw_boot_hash(g_src, len, got);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
        TEST_ASSERT_EQUAL_UINT8(whole + 2u, tfw_model_log_count());
        check_sha(0u, TFW_ATECC_SHA_SHA256_START, 0u, 0u);
        for (i = 0u; i < whole; i++) {
            const tfw_model_cmd_t *c = tfw_model_log((uint8_t)(1u + i));

            check_sha((uint8_t)(1u + i), TFW_ATECC_SHA_SHA256_UPDATE, BLK, BLK);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(&g_src[i * BLK], c->data, BLK);
        }
        check_sha((uint8_t)(1u + whole), TFW_ATECC_SHA_SHA256_END, tail, tail);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
}

/* Kills: a START or UPDATE refusal ignored, END sent after it, sleep skipped.
 * Whatever the step, a refused hash is all zero. */
static void test_a_refused_hash_is_all_zero(void)
{
    static const uint8_t zero[32];
    uint8_t n;

    for (n = 0u; n < 5u; n++) {
        uint8_t out[32];

        setUp();
        tfw_model_fail_commands_after(n);
        memset(out, 0xAAu, sizeof out);
        tfw_boot_hash(g_src, 200u, out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, out, 32);
        TEST_ASSERT_EQUAL_UINT8(n, tfw_model_log_count());
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
    setUp();
    {
        uint8_t out[32];

        tfw_model_set_exec_ms(TFW_ATECC_OP_SHA, 1000u);
        tfw_boot_hash(g_src, 200u, out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, out, 32);
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
    }
    /* One NACKed START and a part that would take the rest: no END follows,
     * or the digest would be of a hash that never started. */
    setUp();
    {
        uint8_t out[32];

        tfw_model_nack_next_command();
        tfw_boot_hash(g_src, 10u, out);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, out, 32);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    }
}

/* Kills: any SHA command given a shorter wait than its datasheet maximum. */
static void test_every_sha_waits_its_maximum_execution_time(void)
{
    uint8_t want[32], got[32];

    tfw_model_set_exec_ms(TFW_ATECC_OP_SHA, TFW_ATECC_T_SHA);
    tfw_sha256(g_src, 200u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(reader, g_base, 200u, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    memset(got, 0, sizeof got);
    tfw_boot_hash(g_src, 200u, got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_measure_takes_one_to_max_bytes);
    RUN_TEST(test_measure_with_no_element_is_a_crypto_error);
    RUN_TEST(test_measure_splits_into_blocks_and_a_tail);
    RUN_TEST(test_a_failed_read_ends_the_measurement);
    RUN_TEST(test_an_element_refusal_ends_the_measurement);
    RUN_TEST(test_measure_reads_program_memory_from_the_app_base);
    RUN_TEST(test_hash_with_no_element_is_all_zero);
    RUN_TEST(test_hash_splits_into_blocks_and_a_tail);
    RUN_TEST(test_a_refused_hash_is_all_zero);
    RUN_TEST(test_every_sha_waits_its_maximum_execution_time);
    return UNITY_END();
}
