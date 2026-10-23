/* The DICE handoff record (src/core/handoff.c over boot/boot_handoff.c's
 * parser) under mutation (issue #163).
 *
 * Every byte of a valid record set to each of its 255 other values, every
 * two-bit error, and the record cut at every length (the rest zero, as cold
 * SRAM, or 0xFF) is read the way test_reset_cause.c and test_crypto_soft.c
 * define an invalid record: no tier, no measurement, no reset cause (the
 * device's own observation stands), no CDI to take, FWHash all zero, the raw
 * block zeroed, and a software provider with no UDS left unprovisioned. Never
 * acted on as valid. A cut that leaves the block byte-identical to the valid
 * one is the valid one, and is read as valid.
 *
 * CRC-16 detects every error of up to 16 bits, so none of these is a
 * collision: each one must be refused. No randomness. The parser is also
 * driven directly on a heap buffer of exactly TFW_BOOT_HANDOFF_LEN bytes, so
 * under `make test-asan` a read past the record is a sanitizer report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "hal_mock.h"
#include "uds_mock.h"
#include "handoff.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "tfw/crypto.h"
#include "tfw/status.h"
#include "crc16.h"
#include "boot_measure.h"

static uint8_t g_valid[TFW_BOOT_HANDOFF_LEN];
static uint8_t g_cdi[32], g_meas[32];

void setUp(void)
{
    unsigned i;

    for (i = 0u; i < 32u; i++) {
        g_cdi[i]  = (uint8_t)(0xD0u + i);
        g_meas[i] = (uint8_t)(0x30u + i);
    }
    tfw_mock_reset();
    tfw_mock_set_uds(0);
    tfw_boot_handoff_build(g_valid, TFW_CRYPTO_SOFTWARE, g_cdi, g_meas, 7u, TFW_RESET_WATCHDOG);
}

void tearDown(void) {}

static int parse_ok(const uint8_t *block)
{
    uint8_t     *h = (uint8_t *)malloc(TFW_BOOT_HANDOFF_LEN);
    uint8_t      tier, cdi[32], meas[32], rc, seed[TFW_BOOT_SEED_LEN];
    uint32_t     ver;
    tfw_status_t st;

    TEST_ASSERT_NOT_NULL(h);
    memcpy(h, block, TFW_BOOT_HANDOFF_LEN);
    st = tfw_boot_handoff_parse(h, &tier, cdi, meas, &ver, &rc, seed);
    free(h);
    return st == TFW_OK;
}

/* Install the block, the device's own observation a brown-out, and initialise. */
static void install(const uint8_t *block)
{
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_mock_set_reset_cause(TFW_RESET_BROWNOUT);
    tfw_boot_handoff_init();
}

static void expect_valid(void)
{
    uint8_t cdi[32], cause = 0u;

    TEST_ASSERT_NOT_NULL(tfw_boot_handoff_tier());
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SOFTWARE, *tfw_boot_handoff_tier());
    TEST_ASSERT_NOT_NULL(tfw_boot_handoff_measurement());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_meas, tfw_boot_handoff_measurement(), 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_meas, tfw_boot_handoff_fw_hash(), 32);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_handoff_reset_cause(&cause));
    TEST_ASSERT_EQUAL_UINT8(TFW_RESET_WATCHDOG, cause);
    TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, tfw_hal_reset_cause());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_handoff_take_cdi(cdi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_cdi, cdi, 32);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, tfw_mock_handoff(), TFW_BOOT_HANDOFF_LEN);
}

static void expect_refused(const char *what, unsigned a, unsigned b)
{
    uint8_t cdi[32], out[32], cause = 0xEEu;
    int     bad = 0;
    unsigned i;

    memset(cdi, 0xA5, sizeof cdi);
    bad |= tfw_boot_handoff_tier() != 0;
    bad |= tfw_boot_handoff_measurement() != 0;
    bad |= tfw_boot_handoff_reset_cause(&cause) != 0u || cause != 0xEEu;
    bad |= tfw_hal_reset_cause() != TFW_RESET_BROWNOUT;
    for (i = 0u; i < 32u; i++) {
        bad |= tfw_boot_handoff_fw_hash()[i] != 0u;
    }
    for (i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        bad |= tfw_mock_handoff()[i] != 0u;
    }
    /* tfw_crypto_init takes the CDI: with no UDS it stays unprovisioned */
    bad |= tfw_crypto_init() != TFW_OK;
    bad |= tfw_crypto_hmac(0u, g_meas, 3u, out) != TFW_ERR_NOT_PROVISIONED;
    bad |= tfw_boot_handoff_take_cdi(cdi) != 0u;
    for (i = 0u; i < 32u; i++) {
        bad |= cdi[i] != 0xA5u;
    }
    if (bad) {
        printf("%s %u/%u\n", what, a, b);
        TEST_FAIL_MESSAGE("an invalid handoff was acted on as valid");
    }
}

static void test_handoff_mutate_the_valid_record_is_read(void)
{
    TEST_ASSERT_TRUE(parse_ok(g_valid));
    install(g_valid);
    expect_valid();
}

static void test_handoff_mutate_every_byte_value_is_refused(void)
{
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    unsigned at, x;

    for (at = 0u; at < TFW_BOOT_HANDOFF_LEN; at++) {
        for (x = 1u; x <= 0xFFu; x++) {
            memcpy(b, g_valid, sizeof b);
            b[at] ^= (uint8_t)x;
            if (parse_ok(b)) {
                printf("byte %u xor 0x%02x\n", at, x);
                TEST_FAIL_MESSAGE("the parser accepted a mutated record");
            }
            install(b);
            expect_refused("byte/xor", at, x);
        }
    }
    install(g_valid);
    expect_valid();
}

static void test_handoff_mutate_every_two_bit_error_is_refused(void)
{
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    unsigned i, j;

    for (i = 0u; i < TFW_BOOT_HANDOFF_LEN * 8u; i++) {
        for (j = i + 1u; j < TFW_BOOT_HANDOFF_LEN * 8u; j++) {
            memcpy(b, g_valid, sizeof b);
            b[i / 8u] ^= (uint8_t)(1u << (i % 8u));
            b[j / 8u] ^= (uint8_t)(1u << (j % 8u));
            if (parse_ok(b)) {
                printf("bits %u/%u\n", i, j);
                TEST_FAIL_MESSAGE("the parser accepted a two-bit error");
            }
        }
        /* the application path, once per first bit (the parser has all) */
        memcpy(b, g_valid, sizeof b);
        b[i / 8u] ^= (uint8_t)(1u << (i % 8u));
        b[((i + 9u) % (TFW_BOOT_HANDOFF_LEN * 8u)) / 8u] ^=
            (uint8_t)(1u << (((i + 9u) % (TFW_BOOT_HANDOFF_LEN * 8u)) % 8u));
        install(b);
        expect_refused("bits", i, (i + 9u) % (TFW_BOOT_HANDOFF_LEN * 8u));
    }
    install(g_valid);
    expect_valid();
}

static void test_handoff_mutate_every_truncation_is_refused(void)
{
    static const uint8_t fills[2] = { 0x00u, 0xFFu };
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    unsigned len, f;

    for (f = 0u; f < 2u; f++) {
        for (len = 0u; len < TFW_BOOT_HANDOFF_LEN; len++) {
            memset(b, fills[f], sizeof b);
            memcpy(b, g_valid, len);
            install(b);
            if (memcmp(b, g_valid, sizeof b) == 0) {
                expect_valid();              /* the cut removed nothing that differed */
            } else {
                expect_refused("cut len/fill", len, fills[f]);
            }
        }
    }
}

/* A record with a correct CRC but a version or magic this build does not know
 * is refused too: the CRC is recomputed over the mutated bytes. */
static void test_handoff_mutate_a_recrced_foreign_version_or_magic_is_refused(void)
{
    uint8_t  b[TFW_BOOT_HANDOFF_LEN];
    unsigned v, bit;
    uint16_t crc;

    for (v = 0u; v <= 0xFFu; v++) {
        if (v == TFW_BOOT_HANDOFF_VERSION) {
            continue;
        }
        memcpy(b, g_valid, sizeof b);
        b[TFW_BOOT_HO_VERSION] = (uint8_t)v;
        crc = tfw_crc16(b, TFW_BOOT_HO_CRC);
        b[TFW_BOOT_HO_CRC]      = (uint8_t)crc;
        b[TFW_BOOT_HO_CRC + 1u] = (uint8_t)(crc >> 8);
        install(b);
        expect_refused("version", v, 0u);
    }
    for (bit = 0u; bit < 32u; bit++) {
        memcpy(b, g_valid, sizeof b);
        b[TFW_BOOT_HO_MAGIC + bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        crc = tfw_crc16(b, TFW_BOOT_HO_CRC);
        b[TFW_BOOT_HO_CRC]      = (uint8_t)crc;
        b[TFW_BOOT_HO_CRC + 1u] = (uint8_t)(crc >> 8);
        install(b);
        expect_refused("magic bit", bit, 0u);
    }
    install(g_valid);
    expect_valid();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_handoff_mutate_the_valid_record_is_read);
    RUN_TEST(test_handoff_mutate_every_byte_value_is_refused);
    RUN_TEST(test_handoff_mutate_every_two_bit_error_is_refused);
    RUN_TEST(test_handoff_mutate_every_truncation_is_refused);
    RUN_TEST(test_handoff_mutate_a_recrced_foreign_version_or_magic_is_refused);
    return UNITY_END();
}
