/*
 * A mutation test for OTA_ACTIVATE, src/core/ota_activate.c (#163).
 *
 * The rule test_ota_stage.c defines: ACTIVATE takes the held OTA_AUTH
 * signature, reads the platform block, refuses TRIAL and CONFIRMED, and marks
 * only the image OTA_END verified. Here it runs in every boot state, a spread
 * of OTA states and every stage state; without the signature; with every mode
 * byte; with every byte of the platform block set to every other value; and
 * with every measured byte of the staged image mutated after OTA_END. Last,
 * SAMPLES cases drawn from a fixed seed. ACTIVATE never writes the platform
 * block. A refusal leaves the block, the stage session and the reset request
 * as they were and no MARK; one before the signature is stored leaves the
 * store untouched too. The original image still activates after each
 * mutation. The seed is a literal; a failure prints it.
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
#include "sha256.h"
#include "tfw/protocol.h"
#include "dispatch.h"

#define SEED     0x0AC71FA7u
#define SAMPLES  3000u
#define TOTAL    512u
#define BODY     200u
#define TRAILER  128u
#define CHUNK    60u      /* TFW_MAX_PAYLOAD less the offset */
#define TR       (TOTAL - TRAILER)

typedef struct {
    uint8_t  ee[TFW_EEPROM_SIZE];
    uint8_t  stage[5];    /* OTA_STATUS: state, then received */
    uint32_t erases;
    uint32_t programs;
    uint8_t  reset;
    int      marked;
} snap_t;

static uint8_t *img;      /* TOTAL bytes: the image every case starts from */
static uint8_t  meas[32];
static snap_t   before, after;
static char     why[96];
static uint32_t rng;

void setUp(void)
{
    tfw_nor_model_reset();
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

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* test_ota_stage.c's image: BODY bytes, erased padding, the v1 trailer. */
static void build(void)
{
    tfw_sha256_t c;
    uint8_t     *t = &img[TR];
    uint32_t     i;

    memset(img, 0xFF, TOTAL);
    for (i = 0u; i < BODY; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, img, (uint16_t)BODY);
    tfw_sha256_final(&c, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[8], 3u);
    put32(&t[12], BODY);
    memcpy(&t[16], meas, 32u);
}

/* A handler called with exactly n bytes of input, from the heap. */
static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *src, uint8_t n, uint8_t out[44], uint8_t *len)
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
    *len = (uint8_t)tfw_writer_len(&w);
    free(in);
    return st;
}

static tfw_status_t begin(void)
{
    uint8_t in[4], out[44], n;

    put32(in, TOTAL);
    return call(tfw_stage_cmd_begin, in, 4u, out, &n);
}

static void stream(const uint8_t *src, uint32_t to)
{
    uint8_t  in[4u + CHUNK], out[44], n;
    uint32_t off;
    uint8_t  len;

    for (off = 0u; off < to; off += CHUNK) {
        len = (uint8_t)(to - off < CHUNK ? to - off : CHUNK);
        put32(in, off);
        memcpy(&in[4], &src[off], len);
        TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_data, in, (uint8_t)(4u + len), out, &n));
    }
}

static tfw_status_t end(void)
{
    uint8_t out[44], n;

    return call(tfw_stage_cmd_end, NULL, 0u, out, &n);
}

/* OTA_AUTH through the dispatcher (#137), so the next command finds it held. */
static void authorize(void)
{
    uint8_t      sig[TFW_OTA_AUTH_SIG_LEN], out[8];
    uint8_t     *in = malloc(sizeof sig);
    tfw_reader_t r;
    tfw_writer_t w;

    TEST_ASSERT_NOT_NULL(in);
    memset(in, 0xA5, sizeof sig);
    tfw_reader_init(&r, in, (uint8_t)sizeof sig);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_OTA_AUTH, &r, &w));
    free(in);
}

static tfw_status_t activate(uint8_t mode)
{
    uint8_t      out[44], n;
    tfw_status_t st = call(tfw_otactl_cmd_activate, &mode, 1u, out, &n);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, n, why);
    return st;
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

static void put_block(uint8_t boot_state, uint8_t ota_state)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 7u;
    p.boot_state     = boot_state;
    p.ota_state      = ota_state;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

/* The stage's one sector rewritten from src, behind the session's back. */
static void write_stage(const uint8_t *src)
{
    uint32_t off;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(TFW_OTA_STORE_STAGE_ADDR));
    for (off = 0u; off < TOTAL; off += TFW_STORE_CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(TFW_OTA_STORE_STAGE_ADDR + off, &src[off], TFW_STORE_CHUNK));
    }
}

/* The image staged and ended: the session COMPLETE, nothing marked. */
static void stage_ended(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, begin());
    stream(img, TOTAL);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_FALSE(marked());
}

/* A reset and the image staged and ended again: a pending activation takes
 * no other request (m1100 (3)), and the OTA_BEGIN budget is per boot (#232). */
static void next_boot(void)
{
    tfw_stage_init();
    stage_ended();
}

static void snap(snap_t *s)
{
    uint8_t out[44], n;

    memcpy(s->ee, tfw_mock_eeprom(), sizeof s->ee);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_status, NULL, 0u, out, &n));
    memcpy(s->stage, out, sizeof s->stage);
    s->erases   = tfw_nor_model_erases();
    s->programs = tfw_nor_model_programs();
    s->reset    = tfw_sys_reset_pending();
    s->marked   = marked();
}

/* A refusal: the EEPROM (the platform block with it), the session, the reset
 * request and the MARK as they were; with store set, the store not touched. */
static void unchanged(int store)
{
    snap(&after);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(before.ee, after.ee, sizeof before.ee, why);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(before.stage, after.stage, sizeof before.stage, why);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(before.reset, after.reset, why);
    if (store) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.erases, after.erases, why);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.programs, after.programs, why);
        TEST_ASSERT_EQUAL_INT_MESSAGE(before.marked, after.marked, why);
    } else {
        /* tfw_stage_authorize erased the MARK's sector for the signature
         * (ota_stage.c): a refusal after it leaves no MARK at all. */
        TEST_ASSERT_FALSE_MESSAGE(after.marked, why);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, tfw_nor_model_violations(), why);
}

/* The original image, rewritten, activates: the mutation latched nothing. */
static void original_activates(void)
{
    write_stage(img);
    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, activate(0u), why);
    TEST_ASSERT_TRUE_MESSAGE(marked(), why);
}

static int refuses(uint8_t boot_state)
{
    return boot_state == TFW_BOOT_STATE_TRIAL || boot_state == TFW_BOOT_STATE_CONFIRMED;
}

static void test_every_boot_and_ota_state_with_an_ended_image(void)
{
    static const uint8_t ota[] = { 0u, 1u, 2u, 3u, 4u, 5u, 0x80u, 0xFFu };
    unsigned             bs, o, taken = 0u, refused = 0u;

    for (bs = 0u; bs < 256u; bs++) {
        for (o = 0u; o < sizeof ota; o++) {
            snprintf(why, sizeof why, "boot_state %u ota_state %u", bs, ota[o]);
            next_boot();   /* a pending activation takes no new attempt (m1117 (1)) */
            put_block((uint8_t)bs, ota[o]);
            authorize();
            snap(&before);
            if (refuses((uint8_t)bs)) {
                TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, activate(0u), why);
                unchanged(1);
                refused++;
            } else {
                TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, activate(0u), why);
                TEST_ASSERT_TRUE_MESSAGE(marked(), why);
                snap(&after);
                TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(before.ee, after.ee, sizeof before.ee, why);
                TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(before.stage, after.stage, sizeof before.stage, why);
                taken++;
            }
            /* One signature, one command, either way: without another OTA_AUTH
             * nothing moves, the retry of a pending activation answering its
             * success (m1117 (1)). */
            snap(&before);
            TEST_ASSERT_EQUAL_MESSAGE(refuses((uint8_t)bs) ? TFW_ERR_AUTH_REQUIRED : TFW_OK,
                                      activate(0u), why);
            unchanged(1);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2u * sizeof ota, refused);
    TEST_ASSERT_EQUAL_UINT32(254u * sizeof ota, taken);
}

/* Without OTA_AUTH nothing moves, in any boot state. */
static void test_without_the_signature_nothing_moves(void)
{
    unsigned bs;

    stage_ended();
    for (bs = 0u; bs < 256u; bs++) {
        snprintf(why, sizeof why, "boot_state %u", bs);
        put_block((uint8_t)bs, TFW_BOOT_OTA_IDLE);
        snap(&before);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_AUTH_REQUIRED, activate((uint8_t)(bs & 1u)), why);
        unchanged(1);
    }
    original_activates();
}

/* IDLE, RECEIVING and FAILED: no ended image, so nothing to activate in any
 * boot state, and the store is never touched. */
static void refused_in_every_boot_state(const char *stage)
{
    unsigned bs;

    for (bs = 0u; bs < 256u; bs++) {
        snprintf(why, sizeof why, "stage %s boot_state %u", stage, bs);
        put_block((uint8_t)bs, TFW_BOOT_OTA_IDLE);
        authorize();
        snap(&before);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, activate((uint8_t)(bs & 1u)), why);
        unchanged(1);
        TEST_ASSERT_FALSE_MESSAGE(marked(), why);
    }
}

static void test_every_stage_state_but_complete_is_refused(void)
{
    uint8_t *bad = malloc(TOTAL);

    TEST_ASSERT_NOT_NULL(bad);
    /* IDLE: a BEGIN no part answered (test_ota_stage.c refused_begin). */
    tfw_nor_shim_reset();
    tfw_nor_model_set_bus(TFW_NOR_MODEL_ABSENT_ECHO);
    TEST_ASSERT_EQUAL(TFW_ERR_NO_STORE, begin());
    tfw_nor_model_set_bus(TFW_NOR_MODEL_PRESENT);
    refused_in_every_boot_state("idle");

    TEST_ASSERT_EQUAL(TFW_OK, begin());
    stream(img, TOTAL - 2u * CHUNK);
    refused_in_every_boot_state("receiving");

    memcpy(bad, img, TOTAL);
    bad[17] ^= 0x40u;
    TEST_ASSERT_EQUAL(TFW_OK, begin());
    stream(bad, TOTAL);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, end());
    refused_in_every_boot_state("failed");
    free(bad);

    stage_ended();
    original_activates();
}

static void test_every_mode_byte(void)
{
    unsigned mode;

    for (mode = 0u; mode < 256u; mode++) {
        snprintf(why, sizeof why, "mode %u", mode);
        next_boot();   /* a pending activation takes no other mode (m1100 (3)) */
        put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
        authorize();
        snap(&before);
        if (mode > 1u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_RANGE, activate((uint8_t)mode), why);
            unchanged(1);
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, activate((uint8_t)mode), why);
            TEST_ASSERT_TRUE_MESSAGE(marked(), why);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(mode, tfw_sys_reset_pending(), why);
            tfw_sys_reset_state();
        }
    }
}

/* The block is CRC-16 to its last byte: one byte changed, to any value, is
 * always caught, so every one is refused before the store is touched. */
static void test_every_byte_of_the_block_at_every_other_value(void)
{
    uint8_t *ee = tfw_mock_eeprom() + TFW_BOOT_EE_BASE;
    unsigned i, x;

    stage_ended();
    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    for (i = 0u; i < TFW_BOOT_EE_LEN; i++) {
        for (x = 1u; x < 256u; x++) {
            tfw_status_t st;

            snprintf(why, sizeof why, "block byte %u xor 0x%02X", i, x);
            ee[i] ^= (uint8_t)x;
            authorize();
            snap(&before);
            st = activate(1u);
            TEST_ASSERT_TRUE_MESSAGE(st == TFW_ERR_CRC || st == TFW_ERR_NOT_PROVISIONED
                                     || st == TFW_ERR_UNSUPPORTED, why);
            unchanged(1);
            TEST_ASSERT_FALSE_MESSAGE(marked(), why);
            ee[i] ^= (uint8_t)x;
        }
    }
    snprintf(why, sizeof why, "block restored");
    original_activates();
}

/* What ACTIVATE measures: the body, and the trailer's magic, version, board,
 * length and measurement. The flags, version and signature are tfw-boot's to
 * check against the signature (boot/boot_policy.c), and the padding is
 * outside the image. */
static int measured(uint32_t off)
{
    if (off < BODY) {
        return 1;
    }
    if (off < TR) {
        return 0;
    }
    off -= TR;
    return off < TFW_BOOT_TR_IMAGE_FLAGS
           || (off >= TFW_BOOT_TR_IMAGE_LEN && off < TFW_BOOT_TR_SIGNED_LEN);
}

static void refused_after_end(const uint8_t *mut, uint8_t mode)
{
    next_boot();   /* the last case's activation is pending */
    write_stage(mut);
    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    snap(&before);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, activate(mode), why);
    unchanged(0);
    original_activates();
}

static void test_every_measured_byte_mutated_after_end_is_refused(void)
{
    uint8_t *mut = malloc(TOTAL);
    uint32_t off, k, cases = 0u;

    TEST_ASSERT_NOT_NULL(mut);
    stage_ended();
    rng = SEED;
    for (off = 0u; off < TOTAL; off++) {
        if (!measured(off)) {
            continue;
        }
        for (k = 0u; k < 3u; k++) {
            uint8_t x = (uint8_t)(rnd() % 255u + 1u);

            snprintf(why, sizeof why, "seed 0x%08X stage byte %lu xor 0x%02X",
                     (unsigned)SEED, (unsigned long)off, x);
            memcpy(mut, img, TOTAL);
            mut[off] ^= x;
            refused_after_end(mut, (uint8_t)(k & 1u));
            cases++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3u * (BODY + TFW_BOOT_TR_IMAGE_FLAGS
                                   + TFW_BOOT_TR_SIGNED_LEN - TFW_BOOT_TR_IMAGE_LEN), cases);
    free(mut);
}

/* SAMPLES cases: a random boot state, and one in four with the stage as
 * ended; the rest with one to eight measured bytes changed at random. */
static void test_seeded_cases_activate_only_the_ended_image(void)
{
    uint8_t *mut = malloc(TOTAL);
    uint32_t it, taken = 0u, refused = 0u;

    TEST_ASSERT_NOT_NULL(mut);
    stage_ended();
    rng = SEED ^ 0x5EEDu;
    for (it = 0u; it < SAMPLES; it++) {
        uint32_t     r  = rnd();
        uint8_t      bs = (uint8_t)(r >> 8);
        uint8_t      n  = (uint8_t)((r >> 16) % 8u + 1u);
        uint8_t      i;
        tfw_status_t st;

        snprintf(why, sizeof why, "seed 0x%08X iteration %lu", (unsigned)(SEED ^ 0x5EEDu),
                 (unsigned long)it);
        next_boot();
        if ((r & 3u) == 0u && (it & 1u)) {
            bs = (uint8_t)((r & 4u) ? TFW_BOOT_STATE_TRIAL : TFW_BOOT_STATE_CONFIRMED);
        }
        memcpy(mut, img, TOTAL);
        if ((r & 3u) != 0u) {
            for (i = 0u; i < n; i++) {
                uint32_t off;

                do {
                    off = rnd() % TOTAL;
                } while (!measured(off));
                mut[off] = (uint8_t)rnd();
            }
        }
        write_stage(mut);
        put_block(bs, (uint8_t)rnd());
        authorize();
        snap(&before);
        st = activate((uint8_t)(r >> 31));
        if (refuses(bs)) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, st, why);
            unchanged(1);
            refused++;
        } else if (memcmp(mut, img, TOTAL) != 0) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_STATE, st, why);
            unchanged(0);
            refused++;
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, why);
            TEST_ASSERT_TRUE_MESSAGE(marked(), why);
            taken++;
        }
        tfw_sys_reset_state();
    }
    /* Proof the loop reached both sides. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, taken);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    snprintf(why, sizeof why, "after the seeded run");
    next_boot();
    original_activates();
    free(mut);
}

int main(void)
{
    int rc;

    img = malloc(TOTAL);
    if (img == NULL) {
        return 1;
    }
    build();
    UNITY_BEGIN();
    RUN_TEST(test_every_boot_and_ota_state_with_an_ended_image);
    RUN_TEST(test_without_the_signature_nothing_moves);
    RUN_TEST(test_every_stage_state_but_complete_is_refused);
    RUN_TEST(test_every_mode_byte);
    RUN_TEST(test_every_byte_of_the_block_at_every_other_value);
    RUN_TEST(test_every_measured_byte_mutated_after_end_is_refused);
    RUN_TEST(test_seeded_cases_activate_only_the_ended_image);
    rc = UNITY_END();
    free(img);
    return rc;
}
