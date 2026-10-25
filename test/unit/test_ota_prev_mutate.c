/*
 * A mutation test for the PREV backup, src/core/ota_prev.c (#163).
 *
 * The rule test_ota_stage.c defines: OTA_BEGIN, after it erases the MARK,
 * copies the whole running image into PREV before it takes a byte, inside
 * PREV's sectors. Here the running image and what PREV held before are drawn
 * from a fixed seed, and the rest of the store is filled the same way: the
 * copy is the running image exactly, whatever PREV held, and nothing outside
 * PREV's sectors moves. Then the backup is torn at every transfer it makes
 * to the part: it answers the store's failure, still writes only inside
 * PREV, and through OTA_BEGIN a torn backup is never acted on -- the session
 * FAILED, no MARK, OTA_ACTIVATE and the MARK check refuse, the platform block
 * as it was -- while the next clean OTA_BEGIN leaves an exact copy. The seed
 * is a literal; a failure prints it.
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
#include "ota_stage.h"
#include "ota_store.h"
#include "ota_ctl.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "caps/sys.h"
#include "tfw/protocol.h"
#include "dispatch.h"

#define SEED     0x9EB0C0F7u
#define ROUNDS   24u
#define SAMPLES  160u
#define TOTAL    512u
#define FAULT    TFW_ERR_TIMEOUT

#define PREV_LO  ((uint32_t)TFW_OTA_STORE_PREV_ADDR)
#define PREV_SPAN \
    ((((uint32_t)TFW_BOOT_APP_SIZE + TFW_STORE_SECTOR - 1u) / TFW_STORE_SECTOR) * TFW_STORE_SECTOR)
#define PREV_HI  (PREV_LO + PREV_SPAN - 1u)

static uint8_t *store;    /* TFW_NOR_MODEL_SIZE: the part as loaded */
static uint8_t *app;      /* TFW_BOOT_APP_SIZE: the running image */
static uint8_t  ee[TFW_BOOT_EE_BLOCK_LEN];
static char     why[96];
static uint32_t rng;

void setUp(void)
{
    tfw_nor_model_reset();
    tfw_nor_shim_reset();
    tfw_sys_reset_state();
}

void tearDown(void) {}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fill(uint8_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)(rnd() >> 24);
    }
}

/* A running image and a whole store from the seed, the store loaded as a
 * programmer would; the platform block GOOD. */
static void load(void)
{
    tfw_boot_platform_t p;

    fill(app, TFW_BOOT_APP_SIZE);
    memcpy(&tfw_mock_flash()[TFW_BOOT_APP_BASE], app, TFW_BOOT_APP_SIZE);
    fill(store, TFW_NOR_MODEL_SIZE);
    tfw_nor_model_load(0u, store, TFW_NOR_MODEL_SIZE);
    memset(&p, 0, sizeof p);
    p.rollback_floor = 7u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_serialise(&p, ee);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, ee, (uint8_t)TFW_BOOT_EE_BLOCK_LEN));
    tfw_nor_model_clear_spans();
}

/* Nothing outside PREV's sectors moved, and nothing was programmed that was
 * not erased first. With mark_too clear, the MARK's sector is OTA_BEGIN's to
 * erase and is left out. */
static void outside_prev_unchanged(int mark_too)
{
    const uint8_t *m      = tfw_nor_model_mem();
    uint32_t       mark   = (uint32_t)TFW_OTA_STORE_MARK_ADDR - ((uint32_t)TFW_OTA_STORE_MARK_ADDR % TFW_STORE_SECTOR);
    uint32_t       a;

    for (a = 0u; a < TFW_NOR_MODEL_SIZE; a += TFW_STORE_SECTOR) {
        if (a >= PREV_LO && a <= PREV_HI) {
            continue;
        }
        if (!mark_too && a == mark) {
            continue;
        }
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&store[a], &m[a], TFW_STORE_SECTOR, why);
    }
}

static void no_violations(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_violations(), why);
}

/* Every erase and program the backup made fell inside PREV's sectors. */
static void inside_prev(void)
{
    if (tfw_nor_model_prog_lo() != 0xFFFFFFFFu) {
        TEST_ASSERT_TRUE_MESSAGE(tfw_nor_model_prog_lo() >= PREV_LO, why);
        TEST_ASSERT_TRUE_MESSAGE(tfw_nor_model_prog_hi() <= PREV_LO + TFW_BOOT_APP_SIZE - 1u, why);
    }
    if (tfw_nor_model_erase_lo() != 0xFFFFFFFFu) {
        TEST_ASSERT_TRUE_MESSAGE(tfw_nor_model_erase_lo() >= PREV_LO, why);
        TEST_ASSERT_TRUE_MESSAGE(tfw_nor_model_erase_hi() <= PREV_HI, why);
    }
}

/* PREV is the running image, and the rest of its last sector erased. */
static void prev_is_the_image(void)
{
    const uint8_t *m = tfw_nor_model_mem();

    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(app, &m[PREV_LO], TFW_BOOT_APP_SIZE, why);
    if (PREV_SPAN > TFW_BOOT_APP_SIZE) {
        TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(0xFFu, &m[PREV_LO + TFW_BOOT_APP_SIZE],
                                             PREV_SPAN - TFW_BOOT_APP_SIZE, why);
    }
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *src, uint8_t n, uint8_t out[44])
{
    uint8_t     *in = malloc(n ? n : 1u);
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    TEST_ASSERT_NOT_NULL(in);
    if (n) { memcpy(in, src, n); }
    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, 44u);
    st = fn(&r, &w);
    free(in);
    return st;
}

static tfw_status_t begin(void)
{
    uint8_t in[4] = { (uint8_t)TOTAL, (uint8_t)(TOTAL >> 8), 0u, 0u }, out[44];

    return call(tfw_stage_cmd_begin, in, 4u, out);
}

static uint8_t stage_state(void)
{
    uint8_t out[44];

    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_status, NULL, 0u, out));
    return out[0];
}

static void authorize(void)
{
    uint8_t      sig[TFW_OTA_AUTH_SIG_LEN], out[8];
    tfw_reader_t r;
    tfw_writer_t w;

    memset(sig, 0xA5, sizeof sig);
    tfw_reader_init(&r, sig, (uint8_t)sizeof sig);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_OTA_AUTH, &r, &w));
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

/* A torn backup behind OTA_BEGIN: nothing anyone could take for a staged or
 * previous image is acted on. */
static void never_acted_on(void)
{
    uint8_t  out[44], m1 = 1u, h[32];
    uint32_t v = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_STAGE_FAILED, stage_state(), why);
    TEST_ASSERT_FALSE_MESSAGE(marked(), why);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, tfw_stage_marked(h, &v, 0u), why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, v, why);
    authorize();
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, call(tfw_otactl_cmd_activate, &m1, 1u, out), why);
    TEST_ASSERT_FALSE_MESSAGE(marked(), why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_sys_reset_pending(), why);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(ee, tfw_mock_eeprom() + TFW_BOOT_EE_BASE, TFW_BOOT_EE_BLOCK_LEN, why);
}

/* Seeded running images over seeded stale PREVs: the copy is exact. */
static void test_the_copy_is_the_running_image_whatever_prev_held(void)
{
    uint32_t round;

    rng = SEED;
    for (round = 0u; round < ROUNDS; round++) {
        snprintf(why, sizeof why, "seed 0x%08X round %lu", (unsigned)SEED, (unsigned long)round);
        setUp();
        load();
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_store_init(), why);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_ota_prev_backup(), why);
        prev_is_the_image();
        inside_prev();
        outside_prev_unchanged(1);
        no_violations();
    }
}

/* Every transfer the backup makes, failed in turn: the store's failure comes
 * back, and the part moved only inside PREV. */
static void test_a_backup_torn_at_every_transfer(void)
{
    uint32_t total, nth;

    rng = SEED ^ 0x70u;
    load();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    tfw_nor_shim_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_prev_backup());
    total = tfw_nor_shim_xfers();
    TEST_ASSERT_GREATER_THAN_UINT32(2u * (TFW_BOOT_APP_SIZE / 64u), total);

    for (nth = 1u; nth <= total; nth++) {
        snprintf(why, sizeof why, "seed 0x%08X transfer %lu of %lu", (unsigned)(SEED ^ 0x70u),
                 (unsigned long)nth, (unsigned long)total);
        tfw_nor_model_reset();
        tfw_nor_model_load(0u, store, TFW_NOR_MODEL_SIZE);
        tfw_nor_model_clear_spans();
        tfw_nor_shim_fail(nth, FAULT);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, tfw_ota_prev_backup(), why);
        inside_prev();
        outside_prev_unchanged(1);
        no_violations();
    }
}

/* OTA_BEGIN with the backup torn at a seeded transfer (and the first and
 * last few): the begin answers the failure and nothing is acted on; a clean
 * OTA_BEGIN after it copies PREV exactly. */
static void test_a_torn_backup_behind_begin_is_never_acted_on(void)
{
    uint32_t pre, total, k, nth;

    rng = SEED ^ 0xB6u;
    load();
    tfw_nor_shim_reset();
    TEST_ASSERT_EQUAL(TFW_OK, begin());
    total = tfw_nor_shim_xfers();
    tfw_nor_model_reset();
    tfw_nor_model_load(0u, store, TFW_NOR_MODEL_SIZE);
    tfw_nor_shim_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_probe());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(TFW_OTA_STORE_MARK_ADDR));
    pre = tfw_nor_shim_xfers();   /* begin's probe and the MARK's erase: the backup follows */
    TEST_ASSERT_LESS_THAN_UINT32(total, pre);

    for (k = 0u; k < SAMPLES; k++) {
        if (k < 8u) {
            nth = pre + 1u + k;
        } else if (k < 16u) {
            nth = total - (k - 8u);
        } else {
            nth = pre + 1u + rnd() % (total - pre);
        }
        snprintf(why, sizeof why, "seed 0x%08X begin transfer %lu of %lu", (unsigned)(SEED ^ 0xB6u),
                 (unsigned long)nth, (unsigned long)total);
        tfw_nor_model_reset();
        tfw_nor_model_load(0u, store, TFW_NOR_MODEL_SIZE);
        tfw_nor_model_clear_spans();
        tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, ee, (uint8_t)TFW_BOOT_EE_BLOCK_LEN);
        tfw_nor_shim_fail(nth, FAULT);
        tfw_stage_init();   /* a boot: a spent OTA_BEGIN budget would refuse before the cut (#232) */
        TEST_ASSERT_NOT_EQUAL_MESSAGE(TFW_OK, begin(), why);
        no_violations();
        never_acted_on();
        outside_prev_unchanged(0);
        no_violations();
    }

    snprintf(why, sizeof why, "seed 0x%08X clean begin", (unsigned)(SEED ^ 0xB6u));
    tfw_nor_shim_reset();
    tfw_nor_model_clear_spans();
    tfw_stage_init();
    TEST_ASSERT_EQUAL(TFW_OK, begin());
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_RECEIVING, stage_state());
    prev_is_the_image();
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

/* The first of PREV's sector erases fails: nothing is copied over it. */
static void test_a_failed_erase_copies_nothing(void)
{
    snprintf(why, sizeof why, "seed 0x%08X first erase", (unsigned)(SEED ^ 0xE5u));
    rng = SEED ^ 0xE5u;
    load();
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_store_init(), why);
    tfw_nor_model_fail_erases(1u, FAULT);
    TEST_ASSERT_EQUAL_MESSAGE(FAULT, tfw_ota_prev_backup(), why);
    tfw_nor_model_fail_erases(0u, TFW_OK);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_programs(), why);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_erases(), why);
    outside_prev_unchanged(1);
    no_violations();
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(&store[PREV_LO], &tfw_nor_model_mem()[PREV_LO], PREV_SPAN, why);
}

int main(void)
{
    int rc;

    store = malloc(TFW_NOR_MODEL_SIZE);
    app   = malloc(TFW_BOOT_APP_SIZE);
    if (store == NULL || app == NULL) {
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_the_copy_is_the_running_image_whatever_prev_held);
    RUN_TEST(test_a_backup_torn_at_every_transfer);
    RUN_TEST(test_a_torn_backup_behind_begin_is_never_acted_on);
    RUN_TEST(test_a_failed_erase_copies_nothing);
    rc = UNITY_END();
    free(store);
    free(app);
    return rc;
}
