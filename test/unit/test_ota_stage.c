/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application-side OTA stager (issue #75) against the software NOR part:
 * src/core/ota_stage.c and src/store/spi_nor.c, linked as an OTA_STAGE=1 image
 * links them, with test/model/spi_nor_model.c as the SPI bus. Every case also
 * asserts the part saw no violation -- a program without WREN, a bit set back
 * to 1, a page wrap, a command while busy -- since the part would lose those
 * bytes without telling anyone.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "sha256.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */

static uint8_t img[TFW_OTA_STORE_STAGE_LEN];
static uint8_t meas[32];

void setUp(void)    { tfw_nor_model_reset(); }
void tearDown(void) { }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* A body of image_len bytes, erased padding, and the 128-byte v1 trailer. */
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
    put32(&t[8], 3u);
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

static void expect_status(uint8_t state, uint32_t received)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_status, NULL, 0u, out));
    TEST_ASSERT_EQUAL_UINT8(state, out[0]);
    TEST_ASSERT_EQUAL_UINT32(received, (uint32_t)out[1] | ((uint32_t)out[2] << 8) |
                                       ((uint32_t)out[3] << 16) | ((uint32_t)out[4] << 24));
}

static void stream(uint32_t from, uint32_t to)
{
    uint32_t off;
    for (off = from; off < to; off += CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, data(off, (uint8_t)(to - off < CHUNK ? to - off : CHUNK)));
    }
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

static void expect_staged(uint32_t total)
{
    const uint8_t *m = tfw_nor_model_mem();
    TEST_ASSERT_EQUAL_MEMORY(img, m + TFW_OTA_STORE_STAGE_ADDR, total);
    TEST_ASSERT_TRUE(marked());
    TEST_ASSERT_EQUAL_UINT8(total & 0xFFu, m[TFW_OTA_STORE_MARK_ADDR + 4u]);
    TEST_ASSERT_EQUAL_UINT8((total >> 8) & 0xFFu, m[TFW_OTA_STORE_MARK_ADDR + 5u]);
    TEST_ASSERT_EQUAL_MEMORY(meas, m + TFW_OTA_STORE_MARK_ADDR + 8u, 32u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

static void test_short_image_is_staged_and_marked(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    expect_status(TFW_STAGE_RECEIVING, 0u);
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    expect_status(TFW_STAGE_COMPLETE, 512u);
    expect_staged(512u);
}

static void test_slot_sized_image_fills_stage(void)
{
    uint32_t total = TFW_OTA_STORE_STAGE_LEN;

    tfw_nor_model_set_busy_polls(3u);
    build(total, total - TRAILER);
    TEST_ASSERT_EQUAL(TFW_OK, begin(total));
    stream(0u, total);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    expect_status(TFW_STAGE_COMPLETE, total);
    expect_staged(total);
    /* The MARK sector, then each STAGE sector once as the stream enters it. */
    TEST_ASSERT_EQUAL_UINT32(1u + total / 4096u, tfw_nor_model_erases());
}

static void test_write_past_the_end_is_refused(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(TFW_OTA_STORE_STAGE_LEN + 256u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(500u));          /* not whole pages */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(TRAILER));       /* no room for a body */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 480u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(480u, CHUNK));    /* 540 > 512 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(0u, CHUNK));      /* not the next offset */
    TEST_ASSERT_EQUAL(TFW_ERR_FRAMING, end());
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

static void test_interrupted_stream_then_fresh_begin(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_TRUE(marked());

    /* A new session withdraws the old record before a byte of STAGE moves. */
    build(1024u, 700u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1024u));
    TEST_ASSERT_FALSE(marked());
    stream(0u, 300u);
    expect_status(TFW_STAGE_RECEIVING, 300u);

    /* The host gave up mid-stream and starts over with a different image. */
    build(768u, 500u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(768u));
    expect_status(TFW_STAGE_RECEIVING, 0u);
    stream(0u, 768u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    expect_status(TFW_STAGE_COMPLETE, 768u);
    expect_staged(768u);
}

static void test_hash_mismatch_fails_without_a_mark(void)
{
    build(512u, 200u);
    img[17] ^= 0x40u;                     /* the body no longer matches its trailer */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, end());
    expect_status(TFW_STAGE_FAILED, 512u);
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

/* Start-up parks the NOR's select, and the shared bus's others, high and driven
 * without a byte on the bus or the SPI peripheral configured: main.c calls
 * this before the transport's first transfer (review #93 P2). */
static void test_init_parks_selects_before_any_transfer(void)
{
    static const tfw_pin_t sel[] = { TFW_OTA_STORE_CS_PIN, TFW_ETH_CS_PIN, TFW_ETH_SD_CS_PIN };
    tfw_pinref_t ref;
    uint8_t      i;

    tfw_mock_reset();
    tfw_stage_init();
    for (i = 0u; i < (uint8_t)(sizeof sel / sizeof sel[0]); i++) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(sel[i], &ref));
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_is_output(ref.port, ref.bit));
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(ref.port, ref.bit));
    }
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_short_image_is_staged_and_marked);
    RUN_TEST(test_slot_sized_image_fills_stage);
    RUN_TEST(test_write_past_the_end_is_refused);
    RUN_TEST(test_interrupted_stream_then_fresh_begin);
    RUN_TEST(test_hash_mismatch_fails_without_a_mark);
    RUN_TEST(test_init_parks_selects_before_any_transfer);
    return UNITY_END();
}
