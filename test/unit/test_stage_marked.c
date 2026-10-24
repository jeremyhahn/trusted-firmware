/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_stage_marked (src/core/ota_stage.c), the read behind OTA_INFO's marked
 * image, called directly: every refusal answers TFW_ERR_BAD_STATE with the
 * version zeroed. Each refusal is reached by flipping one bit of the MARK
 * record or the staged stream in the software NOR part, never through src/.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "boot_platform.h"
#include "sha256.h"
#include "boot_measure.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */
#define MARK      ((uint32_t)TFW_OTA_STORE_MARK_ADDR)
#define NOT_SET   0xA5A5A5A5u

static uint8_t img[TFW_OTA_STORE_STAGE_LEN];
static uint8_t meas[32];

void setUp(void)    { tfw_nor_model_reset(); }
void tearDown(void) { }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* test_ota_stage.c's image: image_len bytes of pattern, the rest erased, and
 * a trailer whose image version is 3 and whose measurement is meas. */
static void build(uint32_t total, uint32_t image_len)
{
    tfw_sha256_t c;
    uint8_t     *t = &img[total - TRAILER];
    uint32_t     i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < image_len; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    tfw_sha256_init(&c);
    for (i = 0u; i < image_len; i += 1024u) {
        tfw_sha256_update(&c, &img[i], (uint16_t)(image_len - i < 1024u ? image_len - i : 1024u));
    }
    tfw_sha256_final(&c, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[TFW_BOOT_TR_IMAGE_VERSION], 3u);
    put32(&t[12], image_len);
    memcpy(&t[16], meas, 32u);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n, uint8_t *out)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, 8u);
    return fn(&r, &w);
}

static tfw_status_t begin(uint32_t total)
{
    uint8_t in[4], out[8];
    put32(in, total);
    return call(tfw_stage_cmd_begin, in, 4u, out);
}

static tfw_status_t data(uint32_t off, uint8_t n)
{
    uint8_t in[4u + CHUNK], out[8];
    put32(in, off);
    memcpy(&in[4], &img[off], n);
    return call(tfw_stage_cmd_data, in, (uint8_t)(4u + n), out);
}

static tfw_status_t end(void)
{
    uint8_t out[8];
    return call(tfw_stage_cmd_end, NULL, 0u, out);
}

static void stream(uint32_t from, uint32_t to)
{
    uint32_t off;
    for (off = from; off < to; off += CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, data(off, (uint8_t)(to - off < CHUNK ? to - off : CHUNK)));
    }
}

/* A whole round, ended and activated, so the MARK record is written. */
static void good_round(uint32_t total, uint32_t image_len)
{
    build(total, image_len);
    TEST_ASSERT_EQUAL(TFW_OK, begin(total));
    stream(0u, total);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
}

static void expect_refused(uint8_t full)
{
    uint8_t  h[TFW_BOOT_MEASURE_LEN];
    uint32_t v = NOT_SET;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_marked(h, &v, full));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
}

static void test_good_round_answers_both_ways(void)
{
    uint8_t  h[TFW_BOOT_MEASURE_LEN];
    uint32_t v = NOT_SET;

    good_round(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_marked(h, &v, 0u));
    TEST_ASSERT_EQUAL_UINT32(3u, v);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, h, 32u);
    v = NOT_SET;
    memset(h, 0, sizeof h);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_marked(h, &v, 1u));
    TEST_ASSERT_EQUAL_UINT32(3u, v);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, h, 32u);
}

static void test_refused_while_receiving(void)
{
    good_round(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 240u);
    expect_refused(0u);
    expect_refused(1u);
    stream(240u, 512u);                      /* leave the stager COMPLETE */
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void test_refused_with_no_mark(void)
{
    expect_refused(0u);                      /* erased: magic 0xFFFFFFFF */
}

static void test_refused_on_a_wrong_magic(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(MARK);             /* 0x47534654 -> 0x47534655 */
    expect_refused(0u);
}

static void test_refused_on_a_length_within_the_trailer(void)
{
    good_round(256u, 100u);
    tfw_nor_model_corrupt(MARK + 5u);        /* 0x100 -> 0x000 */
    expect_refused(0u);
}

static void test_refused_on_a_length_over_the_stage(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(MARK + 6u);        /* 0x200 -> 0x10200, over 0x7000 */
    expect_refused(0u);
}

static void test_refused_on_a_length_off_the_page(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(MARK + 4u);        /* 0x200 -> 0x201 */
    expect_refused(0u);
}

static void test_refused_on_a_mark_hash_unlike_the_trailer(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(MARK + 8u);        /* the MARK's first hash byte */
    expect_refused(0u);
}

static void test_refused_on_a_stream_flipped_after_end(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(TFW_OTA_STORE_STAGE_ADDR + 17u);
    expect_refused(1u);
}

/* Each store read the query makes is one transfer to the part: the MARK, then
 * the trailer's measurement, then its image version. A read that fails is a
 * refusal, never an answer from what was left in the buffer. */
static void test_refused_when_the_mark_cannot_be_read(void)
{
    good_round(512u, 200u);
    tfw_nor_model_fail_xfer(1u);
    expect_refused(0u);
}

static void test_refused_when_the_trailer_measurement_cannot_be_read(void)
{
    good_round(512u, 200u);
    tfw_nor_model_fail_xfer(2u);
    expect_refused(0u);
}

static void test_refused_when_the_image_version_cannot_be_read(void)
{
    good_round(512u, 200u);
    tfw_nor_model_fail_xfer(3u);
    expect_refused(0u);
}

static void test_a_full_check_refuses_a_mark_hash_unlike_the_stream(void)
{
    good_round(512u, 200u);
    tfw_nor_model_corrupt(MARK + 8u);
    expect_refused(1u);
}

static void test_activate_before_an_end_is_refused_and_writes_no_mark(void)
{
    uint32_t progs;

    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 240u);
    progs = tfw_nor_model_programs();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_activate());
    TEST_ASSERT_EQUAL_UINT32(progs, tfw_nor_model_programs());
    expect_refused(0u);
}

static void test_an_end_short_of_the_length_fails_the_stage_and_takes_no_more_data(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 240u);
    TEST_ASSERT_EQUAL(TFW_ERR_FRAMING, end());
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(240u, CHUNK));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
}

static void test_a_session_before_an_end_is_refused_and_leaves_its_outputs(void)
{
    uint8_t  h[TFW_BOOT_MEASURE_LEN];
    uint8_t  was[TFW_BOOT_MEASURE_LEN];
    uint32_t len = NOT_SET;

    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    memset(h, 0x5A, sizeof h);
    memset(was, 0x5A, sizeof was);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_session(&len, h));
    TEST_ASSERT_EQUAL_UINT32(NOT_SET, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(was, h, sizeof h);
}

static void test_authorize_before_an_end_is_refused_and_erases_nothing(void)
{
    uint8_t  sig[TFW_OTA_AUTH_SIG_LEN];
    uint32_t erases;
    uint32_t progs;

    memset(sig, 0x11, sizeof sig);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    erases = tfw_nor_model_erases();
    progs  = tfw_nor_model_programs();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_authorize(sig, 1u));
    TEST_ASSERT_EQUAL_UINT32(erases, tfw_nor_model_erases());
    TEST_ASSERT_EQUAL_UINT32(progs, tfw_nor_model_programs());
}

static void test_authorize_returns_a_mark_erase_failure_and_programs_nothing(void)
{
    uint8_t  sig[TFW_OTA_AUTH_SIG_LEN];
    uint32_t progs;

    memset(sig, 0x11, sizeof sig);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    progs = tfw_nor_model_programs();
    tfw_nor_model_fail_erases(1u, TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_stage_authorize(sig, 1u));
    TEST_ASSERT_EQUAL_UINT32(progs, tfw_nor_model_programs());
}

static void test_no_install_is_pending_when_the_mark_cannot_be_read(void)
{
    good_round(512u, 200u);
    tfw_nor_model_fail_xfer(1u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_stage_install_pending());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_good_round_answers_both_ways);
    RUN_TEST(test_refused_while_receiving);
    RUN_TEST(test_refused_with_no_mark);
    RUN_TEST(test_refused_on_a_wrong_magic);
    RUN_TEST(test_refused_on_a_length_within_the_trailer);
    RUN_TEST(test_refused_on_a_length_over_the_stage);
    RUN_TEST(test_refused_on_a_length_off_the_page);
    RUN_TEST(test_refused_on_a_mark_hash_unlike_the_trailer);
    RUN_TEST(test_refused_on_a_stream_flipped_after_end);
    RUN_TEST(test_refused_when_the_mark_cannot_be_read);
    RUN_TEST(test_refused_when_the_trailer_measurement_cannot_be_read);
    RUN_TEST(test_refused_when_the_image_version_cannot_be_read);
    RUN_TEST(test_a_full_check_refuses_a_mark_hash_unlike_the_stream);
    RUN_TEST(test_activate_before_an_end_is_refused_and_writes_no_mark);
    RUN_TEST(test_an_end_short_of_the_length_fails_the_stage_and_takes_no_more_data);
    RUN_TEST(test_a_session_before_an_end_is_refused_and_leaves_its_outputs);
    RUN_TEST(test_authorize_before_an_end_is_refused_and_erases_nothing);
    RUN_TEST(test_authorize_returns_a_mark_erase_failure_and_programs_nothing);
    RUN_TEST(test_no_install_is_pending_when_the_mark_cannot_be_read);
    return UNITY_END();
}
