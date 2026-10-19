/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation of the update request (src/core/update_request.c writes it,
 * boot/boot_request.c reads it), the bytes test_update_request arms into
 * tfw_hal_handoff_block() and decodes back: every bit flip, every pair of bit
 * flips, every value of every byte, every truncation with the rest 0x00 or
 * 0xFF, and random requests, each decoded from the handoff block and from an
 * exact-size heap copy so an over-read is the sanitizer's.
 *
 * The oracle is the layout as bytes, include/tfw/boot.h: the magic
 * 0x21445055 and its inverse 0xDEBBAFAA, little-endian at 0 and 4. A request
 * that is not those eight bytes is refused, and so is the right one after any
 * reset but a watchdog reset. Arming writes those eight bytes and nothing
 * else of the block, and a clean arm afterwards decodes as pinned.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "hal_mock.h"
#include "update_request.h"
#include "boot_request.h"

#define RANDOM_RUNS 3000u
#define REST_BYTE   0x5Cu

/* 'U','P','D','!' then its inverse, little-endian. */
static const uint8_t REQ[TFW_BOOT_REQUEST_LEN] = {
    0x55, 0x50, 0x44, 0x21, 0xAA, 0xAF, 0xBB, 0xDE,
};

static uint32_t rng = 0x0DA7E5EDu;
static unsigned runs, refused;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    tfw_mock_reset();
    runs = refused = 0u;
}
void tearDown(void) { }

/* One request through the decoder, from the handoff block and from an
 * exact-size heap copy, after a watchdog reset and after every other cause. */
static void run(const uint8_t *req)
{
    uint8_t *exact = malloc(TFW_BOOT_REQUEST_LEN);
    uint8_t  want  = (uint8_t)(memcmp(req, REQ, sizeof REQ) == 0);
    unsigned c;

    TEST_ASSERT_NOT_NULL(exact);
    memcpy(exact, req, TFW_BOOT_REQUEST_LEN);
    memcpy(tfw_hal_handoff_block(), req, TFW_BOOT_REQUEST_LEN);

    TEST_ASSERT_EQUAL_UINT8(want, tfw_boot_request_valid(exact, TFW_RESET_WATCHDOG));
    TEST_ASSERT_EQUAL_UINT8(want, tfw_boot_request_valid(tfw_hal_handoff_block(), TFW_RESET_WATCHDOG));
    for (c = TFW_RESET_UNKNOWN; c <= TFW_RESET_PANIC; c++) {
        if (c != TFW_RESET_WATCHDOG) {
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(exact, (tfw_reset_cause_t)c));
        }
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req, exact, TFW_BOOT_REQUEST_LEN);   /* read only */
    free(exact);
    refused += (unsigned)!want;
    runs++;
}

/* Arming writes exactly the pinned eight bytes and leaves the rest alone. */
static void arm_and_check(void)
{
    uint8_t *hb = tfw_hal_handoff_block();
    unsigned i;

    memset(hb, REST_BYTE, TFW_BOOT_HANDOFF_LEN);
    tfw_update_request_arm();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ, hb, TFW_BOOT_REQUEST_LEN);
    for (i = TFW_BOOT_REQUEST_LEN; i < TFW_BOOT_HANDOFF_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(REST_BYTE, hb[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_valid(hb, TFW_RESET_WATCHDOG));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(hb, TFW_RESET_POWER_ON));
}

static void test_arm_writes_the_pinned_bytes_and_nothing_else(void)
{
    arm_and_check();
    run(REQ);
    TEST_ASSERT_EQUAL_UINT(0u, refused);
}

static void test_every_bit_flip_is_refused(void)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    unsigned bit;

    for (bit = 0u; bit < TFW_BOOT_REQUEST_LEN * 8u; bit++) {
        memcpy(req, REQ, sizeof req);
        req[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        run(req);
    }
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_REQUEST_LEN * 8u, runs);
    TEST_ASSERT_EQUAL_UINT(runs, refused);
}

static void test_every_pair_of_bit_flips_is_refused(void)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    unsigned a, b;

    for (a = 0u; a < TFW_BOOT_REQUEST_LEN * 8u; a++) {
        for (b = a + 1u; b < TFW_BOOT_REQUEST_LEN * 8u; b++) {
            memcpy(req, REQ, sizeof req);
            req[a / 8u] ^= (uint8_t)(1u << (a % 8u));
            req[b / 8u] ^= (uint8_t)(1u << (b % 8u));
            run(req);
        }
    }
    TEST_ASSERT_EQUAL_UINT(64u * 63u / 2u, runs);
    TEST_ASSERT_EQUAL_UINT(runs, refused);
}

static void test_every_value_of_every_byte(void)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    unsigned i, v;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        for (v = 0u; v < 256u; v++) {
            memcpy(req, REQ, sizeof req);
            req[i] = (uint8_t)v;
            run(req);
        }
    }
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_REQUEST_LEN * 256u, runs);
    /* The one pinned value of each byte is the only one accepted. */
    TEST_ASSERT_EQUAL_UINT(runs - TFW_BOOT_REQUEST_LEN, refused);
}

/* A request written part of the way: the first n bytes, the rest as the
 * SRAM was -- zero, erased-looking 0xFF, or the inverse half missing. */
static void test_every_truncation_is_refused(void)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    unsigned n;

    for (n = 0u; n < TFW_BOOT_REQUEST_LEN; n++) {
        memset(req, 0x00, sizeof req);
        memcpy(req, REQ, n);
        run(req);
        memset(req, 0xFF, sizeof req);
        memcpy(req, REQ, n);
        run(req);
    }
    TEST_ASSERT_EQUAL_UINT(2u * TFW_BOOT_REQUEST_LEN, runs);
    TEST_ASSERT_EQUAL_UINT(runs, refused);
}

/* The magic where the inverse goes and the other way round, both halves the
 * magic, both the inverse: each half right on its own is not enough. */
static void test_swapped_and_repeated_halves_are_refused(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];

    memcpy(req, &REQ[4], 4u); memcpy(&req[4], REQ, 4u);       run(req);
    memcpy(req, REQ, 4u);     memcpy(&req[4], REQ, 4u);       run(req);
    memcpy(req, &REQ[4], 4u); memcpy(&req[4], &REQ[4], 4u);   run(req);
    TEST_ASSERT_EQUAL_UINT(3u, runs);
    TEST_ASSERT_EQUAL_UINT(3u, refused);
}

static void test_random_requests_are_refused(void)
{
    uint8_t  req[TFW_BOOT_REQUEST_LEN];
    unsigned i, j;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        for (j = 0u; j < TFW_BOOT_REQUEST_LEN; j++) {
            req[j] = (uint8_t)xorshift32();
        }
        /* Half keep the right magic, so the inverse is what is tested. */
        if (i & 1u) {
            memcpy(req, REQ, 4u);
        }
        run(req);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
    TEST_ASSERT_EQUAL_UINT(runs, refused);
}

/* The armed block mutated where it lies, then armed again. */
static void test_a_mutated_armed_block_then_a_clean_arm(void)
{
    uint8_t *hb = tfw_hal_handoff_block();
    unsigned i;

    for (i = 0u; i < RANDOM_RUNS; i++) {
        tfw_update_request_arm();
        hb[xorshift32() % TFW_BOOT_REQUEST_LEN] ^= (uint8_t)(1u + xorshift32() % 255u);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(hb, TFW_RESET_WATCHDOG));
        refused++;
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
    arm_and_check();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arm_writes_the_pinned_bytes_and_nothing_else);
    RUN_TEST(test_every_bit_flip_is_refused);
    RUN_TEST(test_every_pair_of_bit_flips_is_refused);
    RUN_TEST(test_every_value_of_every_byte);
    RUN_TEST(test_every_truncation_is_refused);
    RUN_TEST(test_swapped_and_repeated_halves_are_refused);
    RUN_TEST(test_random_requests_are_refused);
    RUN_TEST(test_a_mutated_armed_block_then_a_clean_arm);
    RUN_TEST(test_arm_writes_the_pinned_bytes_and_nothing_else);
    return UNITY_END();
}
