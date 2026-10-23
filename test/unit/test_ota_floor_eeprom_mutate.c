/*
 * A mutation test for the EEPROM floor, src/core/ota_floor_eeprom.c (#163).
 *
 * On a tier with no element OTA_INFO's floor is the platform block's copy,
 * the one tfw-boot enforces. The rule test_otactl_floor.c pins for a good
 * block: the floor reported is the block's. Here the block is mutated, torn
 * and pushed to the ends of its range. Every one-bit flip of a good block is
 * refused with no reply. A floor raise torn after every byte reads as the old
 * floor, the new one, or a refusal -- never a floor below the old one and
 * never one that was not written. Any floor in a good block, 0 and
 * 0xFFFFFFFF included, is reported as written and never clamped. Seeds are
 * literals, and a failure prints the seed it was drawn from.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "spi_nor_hal.h"
#include "ota_ctl.h"
#include "ota_floor.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define SEED     0xF100EE11u
#define PAIRS    64u
#define SAMPLES  4000u
#define OUT      44u
#define UNTOUCH  0xEEu

static char     why[112];
static uint32_t rng;

void setUp(void)
{
    tfw_nor_model_reset();
    tfw_nor_shim_reset();
}

void tearDown(void) {}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* A good block: the floor, the states drawn, the rest zero. */
static void block(uint8_t raw[TFW_BOOT_EE_LEN], uint32_t floor, uint8_t boot, uint8_t ota)
{
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = floor;
    p.boot_state     = boot;
    p.ota_state      = ota;
    tfw_boot_platform_serialise(&p, raw);
}

static void put(const uint8_t *raw)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

/* OTA_INFO for the running slot, its input a heap byte. On OK *floor is the
 * floor it reported; otherwise the reply must be untouched. */
static tfw_status_t info(uint32_t *floor)
{
    uint8_t     *in = malloc(1u);
    uint8_t      out[OUT];
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;
    uint32_t     i;

    TEST_ASSERT_NOT_NULL(in);
    in[0] = 0u;
    memset(out, UNTOUCH, sizeof out);
    tfw_reader_init(&r, in, 1u);
    tfw_writer_init(&w, out, OUT);
    st = tfw_otactl_cmd_info(&r, &w);
    free(in);
    if (st == TFW_OK) {
        tfw_reader_t rep;

        TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, out[0], why);
        tfw_reader_init(&rep, &out[3], 4u);
        *floor = tfw_read_u32(&rep);
    } else {
        for (i = 0u; i < OUT; i++) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(UNTOUCH, out[i], why);
        }
    }
    return st;
}

static const uint8_t states[] = {
    TFW_BOOT_STATE_GOOD, TFW_BOOT_STATE_TRIAL, TFW_BOOT_STATE_CONFIRMED
};

/* The floor in a good block, reported as written: through OTA_INFO and
 * straight from tfw_otactl_floor. The ends and seeded values between. */
static void test_any_floor_in_a_good_block_is_reported_as_written(void)
{
    static const uint32_t ends[] = {
        0u, 1u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u, 0x7FFFFFFFu,
        0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu
    };
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;
    uint32_t            k, f, got;

    rng = SEED;
    for (k = 0u; k < SAMPLES; k++) {
        f = (k < sizeof ends / sizeof ends[0]) ? ends[k] : rnd();
        snprintf(why, sizeof why, "seed 0x%08X case %lu floor 0x%08lX", (unsigned)SEED,
                 (unsigned long)k, (unsigned long)f);
        block(raw, f, states[k % 3u], TFW_BOOT_OTA_IDLE);
        put(raw);
        got = ~f;
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(f, got, why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_boot_platform_parse(raw, &p), why);
        got = ~f;
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_otactl_floor(&p, &got), why);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(f, got, why);
    }
}

/* Every bit of a good block flipped in turn: refused, no reply, and the
 * block as left (OTA_INFO writes nothing back). */
static void test_every_one_bit_flip_is_refused(void)
{
    uint8_t  good[TFW_BOOT_EE_LEN], *raw = malloc(TFW_BOOT_EE_LEN);
    uint32_t pair, bit, f, got;

    TEST_ASSERT_NOT_NULL(raw);
    rng = SEED ^ 0xB1u;
    for (pair = 0u; pair < 4u; pair++) {
        f = (pair == 0u) ? 0u : rnd();
        block(good, f, states[pair % 3u], TFW_BOOT_OTA_IDLE);
        for (bit = 0u; bit < 8u * TFW_BOOT_EE_LEN; bit++) {
            snprintf(why, sizeof why, "seed 0x%08X floor 0x%08lX byte %lu bit %lu",
                     (unsigned)(SEED ^ 0xB1u), (unsigned long)f, (unsigned long)(bit / 8u),
                     (unsigned long)(bit % 8u));
            memcpy(raw, good, TFW_BOOT_EE_LEN);
            raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            put(raw);
            got = 0xA5A5A5A5u;
            TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xA5A5A5A5u, got, why);
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(raw, tfw_mock_eeprom() + TFW_BOOT_EE_BASE,
                                                  TFW_BOOT_EE_LEN, why);
        }
    }
    free(raw);
}

/* A raise from old to new, the write torn after every byte as the EEPROM
 * writes them, in order: the old floor, the new, or refused. Never below
 * the old, never a value no one wrote; the whole write gives the new. */
static void test_a_raise_torn_after_every_byte(void)
{
    uint8_t      before[TFW_BOOT_EE_LEN], after[TFW_BOOT_EE_LEN], *raw = malloc(TFW_BOOT_EE_LEN);
    uint32_t     pair, n, lo, hi, got;
    tfw_status_t st;

    TEST_ASSERT_NOT_NULL(raw);
    rng = SEED ^ 0x7Eu;
    for (pair = 0u; pair < PAIRS; pair++) {
        lo = (pair == 0u) ? 0u : rnd() >> (pair % 32u);
        hi = (pair == 1u) ? 0xFFFFFFFFu : lo + 1u + (rnd() >> (1u + pair % 31u));
        if (hi < lo) {
            hi = 0xFFFFFFFFu;
        }
        block(before, lo, TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
        block(after, hi, TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
        for (n = 0u; n <= TFW_BOOT_EE_LEN; n++) {
            snprintf(why, sizeof why, "seed 0x%08X pair %lu 0x%08lX->0x%08lX torn after %lu",
                     (unsigned)(SEED ^ 0x7Eu), (unsigned long)pair, (unsigned long)lo,
                     (unsigned long)hi, (unsigned long)n);
            memcpy(raw, after, n);
            memcpy(&raw[n], &before[n], TFW_BOOT_EE_LEN - n);
            put(raw);
            got = 0xA5A5A5A5u;
            st = info(&got);
            if (n == 0u) {
                TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, why);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(lo, got, why);
            } else if (n == TFW_BOOT_EE_LEN) {
                TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, why);
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(hi, got, why);
            } else if (st == TFW_OK) {
                TEST_ASSERT_TRUE_MESSAGE(got == lo || got == hi, why);
            }
        }
    }
    free(raw);
}

/* Seeded multi-byte damage over a good block: either refused or, where the
 * damage happened to leave the block's bytes as they were, the floor written. */
static void test_seeded_damage_is_refused(void)
{
    uint8_t  good[TFW_BOOT_EE_LEN], *raw = malloc(TFW_BOOT_EE_LEN);
    uint32_t k, f, got, hits, i;

    TEST_ASSERT_NOT_NULL(raw);
    rng = SEED ^ 0xD4u;
    for (k = 0u; k < SAMPLES; k++) {
        f = rnd();
        block(good, f, states[k % 3u], TFW_BOOT_OTA_IDLE);
        memcpy(raw, good, TFW_BOOT_EE_LEN);
        hits = 2u + rnd() % 6u;
        for (i = 0u; i < hits; i++) {
            raw[rnd() % TFW_BOOT_EE_LEN] ^= (uint8_t)(1u + rnd() % 255u);
        }
        snprintf(why, sizeof why, "seed 0x%08X case %lu floor 0x%08lX", (unsigned)(SEED ^ 0xD4u),
                 (unsigned long)k, (unsigned long)f);
        put(raw);
        got = 0xA5A5A5A5u;
        if (memcmp(raw, good, TFW_BOOT_EE_LEN) == 0) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(f, got, why);
        } else {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
        }
    }
    free(raw);
}

/* Never provisioned (erased 0xFF) and all zero: refused. */
static void test_erased_and_zeroed_blocks_are_refused(void)
{
    uint8_t  raw[TFW_BOOT_EE_LEN];
    uint32_t got = 0u;

    snprintf(why, sizeof why, "erased");
    memset(raw, 0xFF, sizeof raw);
    put(raw);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
    snprintf(why, sizeof why, "zeroed");
    memset(raw, 0x00, sizeof raw);
    put(raw);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, info(&got), why);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_any_floor_in_a_good_block_is_reported_as_written);
    RUN_TEST(test_every_one_bit_flip_is_refused);
    RUN_TEST(test_a_raise_torn_after_every_byte);
    RUN_TEST(test_seeded_damage_is_refused);
    RUN_TEST(test_erased_and_zeroed_blocks_are_refused);
    return UNITY_END();
}
