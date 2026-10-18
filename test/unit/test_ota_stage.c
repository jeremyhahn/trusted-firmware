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
#include "ota_store.h"
#include "ota_ctl.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "caps/sys.h"
#include "sha256.h"
#include "crc16.h"
#include "handoff.h"
#include "boot_measure.h"
#include <string.h>
#include "tfw/protocol.h"
#include "dispatch.h"

#define TRAILER   128u
#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */

static uint8_t img[TFW_OTA_STORE_STAGE_LEN];
static uint8_t meas[32];

void setUp(void)    { tfw_nor_model_reset(); }

/* OTA_AUTH through the dispatcher (#137), so the next command finds it held. */
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
    TEST_ASSERT_FALSE(marked());          /* ended, not activated: never taken */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
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
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
    expect_staged(total);
    /* The MARK sector, PREV's for the backup (#52), then each STAGE sector
     * once as the stream enters it. */
    TEST_ASSERT_EQUAL_UINT32(1u + TFW_BOOT_APP_SIZE / 4096u + total / 4096u,
                             tfw_nor_model_erases());
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
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
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
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
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

/* ---- OTA_INFO, OTA_CONFIRM, OTA_ACTIVATE (#77, ADR decision 8) ---- */

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

static void get_block(uint8_t raw[TFW_BOOT_EE_LEN])
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_read(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

static tfw_status_t ctl(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                        const uint8_t *in, uint8_t n, uint8_t out[44], uint8_t *len)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, 44u);
    st = fn(&r, &w);
    *len = (uint8_t)tfw_writer_len(&w);
    return st;
}

static void test_info_reports_the_block_and_the_marked_image(void)
{
    uint8_t s1 = 1u, s0 = 0u, s9 = 9u, out[44], n;

    put_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_info, &s0, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(44u, n);
    TEST_ASSERT_EQUAL_UINT8(2u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(7u, out[3]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, out[7]);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ctl(tfw_otactl_cmd_info, &s9, 1u, out, &n));

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_info, &s1, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_IDLE, out[7]);      /* nothing marked */
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_info, &s1, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_IDLE, out[7]);      /* ended, not activated */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_info, &s1, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(0x00u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, out[7]);
    TEST_ASSERT_EQUAL_MEMORY(meas, &out[12], TFW_BOOT_MEASURE_LEN);
}

static void test_activate_takes_a_verified_image_and_resets_on_mode_1(void)
{
    uint8_t m0 = 0u, m1 = 1u, m2 = 2u, out[44], n;

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ctl(tfw_otactl_cmd_activate, &m2, 1u, out, &n));
    TEST_ASSERT_FALSE(marked());
    authorize();
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_TRUE(marked());           /* ACTIVATE is the MARK's one writer */
    expect_staged(512u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
    authorize();
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_activate, &m1, 1u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_sys_reset_pending());
    tfw_sys_reset_state();
}

static void test_activate_fails_closed_on_an_unverifiable_block(void)
{
    uint8_t m1 = 1u, out[44], n, before[TFW_BOOT_EE_LEN], after[TFW_BOOT_EE_LEN];

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    build(512u, 200u);
    img[17] ^= 0x40u;                     /* the body no longer matches its trailer */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, end());
    get_block(before);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_activate, &m1, 1u, out, &n));
    get_block(after);
    TEST_ASSERT_EQUAL_MEMORY(before, after, TFW_BOOT_EE_LEN);
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
    expect_status(TFW_STAGE_FAILED, 512u);

    /* Mid-stream: no MARK, refused the same way. */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_activate, &m1, 1u, out, &n));
    /* In TRIAL: the running trial is confirmed or failed first. */
    put_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_activate, &m1, 1u, out, &n));
}

/* Body and trailer replaced together after OTA_END: the new image verifies on
 * its own, but it is not the measurement END checked, so ACTIVATE refuses. */
static void test_activate_takes_only_the_image_end_verified(void)
{
    uint8_t  m0 = 0u, out[44], n, before[TFW_BOOT_EE_LEN], after[TFW_BOOT_EE_LEN];
    uint32_t off;

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    build(512u, 300u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(TFW_OTA_STORE_STAGE_ADDR));
    for (off = 0u; off < 512u; off += TFW_STORE_CHUNK) {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(TFW_OTA_STORE_STAGE_ADDR + off, &img[off], TFW_STORE_CHUNK));
    }
    get_block(before);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    get_block(after);
    TEST_ASSERT_EQUAL_MEMORY(before, after, TFW_BOOT_EE_LEN);
    TEST_ASSERT_FALSE(marked());
}

/* CONFIRM leaves its request for tfw-boot and never writes the platform block
 * (include/tfw/hal.h: tfw-boot is its one writer). */
static void test_confirm_writes_its_request_and_never_the_block(void)
{
    uint8_t   out[44], n, raw[TFW_BOOT_EE_LEN], before[TFW_BOOT_EE_LEN];
    uint8_t   block[TFW_BOOT_HANDOFF_LEN], cdi[32] = { 0 }, m[32];
    uint8_t  *rec = tfw_mock_eeprom() + TFW_BOOT_CONFIRM_ADDR;
    uint16_t  crc;

    memset(m, 0x3Cu, sizeof m);
    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_confirm, NULL, 0u, out, &n));
    /* TRIAL with no handoff: no image to bind the request to. */
    put_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_confirm, NULL, 0u, out, &n));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, rec[0]);

    tfw_boot_handoff_build(block, 0u, cdi, m, 3u, 0u);
    memcpy(tfw_mock_handoff(), block, sizeof block);
    tfw_boot_handoff_init();
    get_block(before);
    authorize();
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_confirm, NULL, 0u, out, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    get_block(raw);
    TEST_ASSERT_EQUAL_MEMORY(before, raw, TFW_BOOT_EE_LEN);   /* still TRIAL */
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_CONFIRM_MAGIC, rec[0]);
    TEST_ASSERT_EQUAL_MEMORY(m, &rec[TFW_BOOT_CONFIRM_O_MEAS], TFW_BOOT_CONFIRM_MEAS_LEN);
    crc = tfw_crc16(rec, TFW_BOOT_CONFIRM_O_CRC);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, rec[TFW_BOOT_CONFIRM_O_CRC]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), rec[TFW_BOOT_CONFIRM_O_CRC + 1u]);
}

/* On a one-slot board (#52), OTA_BEGIN copies the running image -- the whole
 * application region -- into PREV before it takes a byte, and a stream
 * after it leaves PREV as it was. */
static void test_begin_backs_the_running_image_up_to_prev(void)
{
    uint8_t *flash = tfw_mock_flash();
    uint32_t i;

    for (i = 0u; i < TFW_BOOT_APP_SIZE; i++) {
        flash[TFW_BOOT_APP_BASE + i] = (uint8_t)(i * 3u + 11u);
    }
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    TEST_ASSERT_EQUAL_MEMORY(&flash[TFW_BOOT_APP_BASE],
                             &tfw_nor_model_mem()[TFW_OTA_STORE_PREV_ADDR], TFW_BOOT_APP_SIZE);
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL_MEMORY(&flash[TFW_BOOT_APP_BASE],
                             &tfw_nor_model_mem()[TFW_OTA_STORE_PREV_ADDR], TFW_BOOT_APP_SIZE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());   /* the MARK's one writer (#77) */
    expect_staged(512u);
}

/* #137: no held signature, 17 for both commands; nothing marked, nothing written. */
static void test_activate_and_confirm_need_ota_auth(void)
{
    uint8_t m0 = 0u, out[44], n, ping[1] = { 0 };
    tfw_reader_t r;
    tfw_writer_t w;

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_FALSE(marked());
    put_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR] = 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, ctl(tfw_otactl_cmd_confirm, NULL, 0u, out, &n));
    TEST_ASSERT_NOT_EQUAL(TFW_BOOT_CONFIRM_MAGIC, tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR]);
    /* Any other command between drops it; so does a short OTA_AUTH. */
    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    tfw_reader_init(&r, ping, 0u);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_PING, &r, &w));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    authorize();
    tfw_reader_init(&r, ping, 1u);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, tfw_dispatch(TFW_CMD_OTA_AUTH, &r, &w));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_FALSE(marked());
    /* One signature, one command. */
    authorize();
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
}

/* ota_activate.c:57: an authorized ACTIVATE over a corrupted platform block
 * answers the block's own refusal and leaves no MARK. */
static void test_activate_refuses_a_corrupted_platform_block(void)
{
    uint8_t m0 = 0u, out[44], n;

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    tfw_mock_eeprom()[TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN / 2u] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_FALSE(marked());
}

/* #137: the held signature is written after the MARK, for tfw-boot. */
static void test_the_signature_is_stored_with_the_mark(void)
{
    uint8_t m0 = 0u, out[44], n, sig[TFW_OTA_AUTH_SIG_LEN], want[TFW_OTA_AUTH_SIG_LEN];

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    authorize();
    TEST_ASSERT_EQUAL(TFW_OK, ctl(tfw_otactl_cmd_activate, &m0, 1u, out, &n));
    TEST_ASSERT_TRUE(marked());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_read(((uint32_t)TFW_OTA_STORE_MARK_ADDR) + TFW_STAGE_MARK_O_SIG,
                                             sig, (uint8_t)sizeof sig));
    memset(want, 0xA5, sizeof want);
    TEST_ASSERT_EQUAL_MEMORY(want, sig, sizeof sig);
}

/* No part on the select (#75, test-matrix.md #137): nothing in spi_nor.c reads
 * a JEDEC id (ADR open question 2), so these pin what the stager does today. */
static void refuse_activate(void)
{
    uint8_t m1 = 1u, out[44], n;

    put_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    authorize();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, ctl(tfw_otactl_cmd_activate, &m1, 1u, out, &n));
    TEST_ASSERT_FALSE(marked());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
}

static void test_absent_nor_on_an_echo_bus_fails_closed_at_end(void)
{
    tfw_nor_model_set_bus(TFW_NOR_MODEL_ABSENT_ECHO);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));   /* WIP reads clear: every erase "succeeds" */
    stream(0u, 512u);                         /* and every program */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, end());   /* the trailer magic, ota_stage.c:79 */
    expect_status(TFW_STAGE_FAILED, 512u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
    refuse_activate();
}

static void test_absent_nor_on_a_high_bus_times_out_the_first_erase(void)
{
    tfw_ms_t t0 = tfw_hal_millis();

    tfw_nor_model_set_bus(TFW_NOR_MODEL_ABSENT_HIGH);
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, begin(512u));   /* WIP never clears, spi_nor.c wait_ready */
    TEST_ASSERT_TRUE((tfw_ms_t)(tfw_hal_millis() - t0) > 0u);
    /* Not RECEIVING after the failed BEGIN: ota_stage.c:172 and :202 answer BUSY. */
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(0u, CHUNK));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
    refuse_activate();
}

/* spi_nor.c bounds a length, never an address: the callers keep the store's
 * addresses inside their regions (#75). */
#define STAGE_END ((uint32_t)TFW_OTA_STORE_STAGE_ADDR + (uint32_t)TFW_OTA_STORE_STAGE_LEN)

static void test_an_oversize_begin_touches_nothing(void)
{
    /* ota_stage.c:142 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(TFW_OTA_STORE_STAGE_LEN + 256u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(0xFFFFFF00u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

static void test_data_stays_inside_the_declared_length(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    tfw_nor_model_clear_spans();
    stream(0u, 480u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(480u, CHUNK));   /* ota_stage.c:175 */
    TEST_ASSERT_EQUAL_UINT32(TFW_OTA_STORE_STAGE_ADDR, tfw_nor_model_prog_lo());
    TEST_ASSERT_EQUAL_UINT32(TFW_OTA_STORE_STAGE_ADDR + 479u, tfw_nor_model_prog_hi());
    TEST_ASSERT_TRUE(tfw_nor_model_erase_hi() < STAGE_END);
    TEST_ASSERT_TRUE(tfw_nor_model_erase_lo() == 0xFFFFFFFFu ||
                     tfw_nor_model_erase_lo() >= TFW_OTA_STORE_STAGE_ADDR);
}

static void test_a_trailer_image_len_past_its_trailer_is_refused(void)
{
    build(512u, 200u);
    put32(&img[512u - TRAILER + 12u], 512u - TRAILER + 1u);   /* reaches its own trailer */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    tfw_nor_model_clear_spans();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, end());                  /* ota_stage.c:89 */
    expect_status(TFW_STAGE_FAILED, 512u);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, tfw_nor_model_prog_lo());
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, tfw_nor_model_erase_lo());
    refuse_activate();
}

static int disjoint(uint32_t alo, uint32_t ahi, uint32_t blo, uint32_t bhi)
{
    return ahi < blo || alo > bhi;
}

static void test_the_prev_copy_stays_in_its_region(void)
{
    uint32_t sector  = (uint32_t)TFW_STORE_SECTOR;
    uint32_t sectors = ((uint32_t)TFW_BOOT_APP_SIZE + sector - 1u) / sector;
    uint32_t lo      = (uint32_t)TFW_OTA_STORE_PREV_ADDR;
    uint32_t hi      = lo + sectors * sector - 1u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_prev_backup());
    TEST_ASSERT_EQUAL_UINT32(lo, tfw_nor_model_prog_lo());
    TEST_ASSERT_EQUAL_UINT32(lo + TFW_BOOT_APP_SIZE - 1u, tfw_nor_model_prog_hi());
    TEST_ASSERT_EQUAL_UINT32(lo, tfw_nor_model_erase_lo());
    TEST_ASSERT_EQUAL_UINT32(hi, tfw_nor_model_erase_hi());   /* whole sectors */
    TEST_ASSERT_TRUE(disjoint(lo, hi, TFW_OTA_STORE_STAGE_ADDR, STAGE_END - 1u));
    TEST_ASSERT_TRUE(disjoint(lo, hi, TFW_OTA_STORE_MARK_ADDR, TFW_OTA_STORE_MARK_ADDR));
    TEST_ASSERT_LESS_THAN_UINT32(TFW_NOR_MODEL_SIZE, hi);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

/* ota_stage.c tfw_stage_cmd_data, the write failure: a chunk longer than one
 * store write takes (TFW_STORE_CHUNK) is refused by the store, and the stage
 * fails. The wire cannot carry it (CHUNK is 60); the handler's reader can. */
static void test_a_chunk_the_store_refuses_fails_the_stage(void)
{
    uint8_t in[4u + TFW_STORE_CHUNK + 1u], out[8];

    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    put32(in, 0u);
    memcpy(&in[4], img, TFW_STORE_CHUNK + 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call(tfw_stage_cmd_data, in, sizeof in, out));
    expect_status(TFW_STAGE_FAILED, 0u);
}

static void stage_and_mark(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_activate());
}

/* tfw_stage_marked with full set re-hashes the stage and answers the mark. */
static void test_a_full_check_reads_the_marked_image(void)
{
    uint8_t  h[32];
    uint32_t v;

    stage_and_mark();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_marked(h, &v, 1u));
    TEST_ASSERT_EQUAL_MEMORY(meas, h, 32u);
    TEST_ASSERT_EQUAL_UINT32(3u, v);
}

static void test_a_full_check_refuses_a_stage_body_that_changed(void)
{
    uint8_t  h[32];
    uint32_t v;

    stage_and_mark();
    tfw_nor_model_corrupt(TFW_OTA_STORE_STAGE_ADDR + 17u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_marked(h, &v, 1u));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
}

/* The quick check: the trailer's measurement no longer the mark's hash. */
static void test_a_trailer_measurement_unlike_the_mark_is_refused(void)
{
    uint8_t  h[32];
    uint32_t v;

    stage_and_mark();
    tfw_nor_model_corrupt(TFW_OTA_STORE_STAGE_ADDR + 512u - TRAILER + TFW_BOOT_TR_MEASUREMENT);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_marked(h, &v, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
}

/* ota_stage.c:156: the mark's erase fails in begin; fail() leaves FAILED. */
static void test_begin_fails_on_a_store_erase_failure(void)
{
    build(512u, 200u);
    tfw_nor_model_fail_erases(1u, TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, begin(512u));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(0u, CHUNK));   /* not RECEIVING */
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));             /* the switch is spent */
    expect_status(TFW_STAGE_RECEIVING, 0u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

/* ota_stage.c:183: a stage sector's erase fails in data; fail() leaves FAILED
 * with nothing received, and a fresh begin recovers. */
static void test_data_fails_on_a_store_erase_failure(void)
{
    build(512u, 200u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    tfw_nor_model_fail_erases(1u, TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, data(0u, CHUNK));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(0u, CHUNK));
    TEST_ASSERT_EQUAL(TFW_OK, begin(512u));
    stream(0u, 512u);
    expect_status(TFW_STAGE_RECEIVING, 512u);
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

/* src/store/spi_nor.c's error paths, through the model's fault switches. An
 * erase is WREN, ERASE, then status polls; a write of one page is WREN,
 * PROGRAM, then status polls; a read is one transfer. Each failing transfer's
 * status comes back unchanged. */
void test_the_store_hands_back_a_failed_erase_transfer(void)
{
    uint8_t nth;

    for (nth = 1u; nth <= 3u; nth++) {
        tfw_nor_model_reset();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
        tfw_nor_model_fail_xfer(nth);
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_store_erase(TFW_OTA_STORE_STAGE_ADDR));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
}

void test_the_store_hands_back_a_failed_write_transfer(void)
{
    static const uint8_t src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t nth;

    for (nth = 1u; nth <= 2u; nth++) {
        tfw_nor_model_reset();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
        tfw_nor_model_fail_xfer(nth);
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_store_write(TFW_OTA_STORE_STAGE_ADDR, src, sizeof src));
    }
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

void test_the_store_hands_back_a_failed_read_transfer(void)
{
    uint8_t buf[8];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    memset(buf, 0xA5, sizeof buf);
    tfw_nor_model_fail_xfer(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_store_read(TFW_OTA_STORE_STAGE_ADDR, buf, sizeof buf));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, buf, sizeof buf);
}

void test_the_store_refuses_more_than_a_chunk(void)
{
    uint8_t buf[TFW_STORE_CHUNK + 1u];

    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_read(TFW_OTA_STORE_STAGE_ADDR, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_write(TFW_OTA_STORE_STAGE_ADDR, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

void test_a_part_that_stays_busy_times_the_erase_out(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    tfw_mock_set_millis(0u);
    tfw_nor_model_set_busy_polls(255u);
    tfw_nor_model_set_xfer_ms(20u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_store_erase(TFW_OTA_STORE_STAGE_ADDR));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nor_model_erases());
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
    RUN_TEST(test_info_reports_the_block_and_the_marked_image);
    RUN_TEST(test_activate_takes_a_verified_image_and_resets_on_mode_1);
    RUN_TEST(test_activate_fails_closed_on_an_unverifiable_block);
    RUN_TEST(test_activate_takes_only_the_image_end_verified);
    RUN_TEST(test_confirm_writes_its_request_and_never_the_block);
    RUN_TEST(test_begin_backs_the_running_image_up_to_prev);
    RUN_TEST(test_activate_and_confirm_need_ota_auth);
    RUN_TEST(test_activate_refuses_a_corrupted_platform_block);
    RUN_TEST(test_the_signature_is_stored_with_the_mark);
    RUN_TEST(test_absent_nor_on_an_echo_bus_fails_closed_at_end);
    RUN_TEST(test_absent_nor_on_a_high_bus_times_out_the_first_erase);
    RUN_TEST(test_an_oversize_begin_touches_nothing);
    RUN_TEST(test_data_stays_inside_the_declared_length);
    RUN_TEST(test_a_trailer_image_len_past_its_trailer_is_refused);
    RUN_TEST(test_the_prev_copy_stays_in_its_region);
    RUN_TEST(test_a_chunk_the_store_refuses_fails_the_stage);
    RUN_TEST(test_a_full_check_reads_the_marked_image);
    RUN_TEST(test_a_full_check_refuses_a_stage_body_that_changed);
    RUN_TEST(test_a_trailer_measurement_unlike_the_mark_is_refused);
    RUN_TEST(test_begin_fails_on_a_store_erase_failure);
    RUN_TEST(test_data_fails_on_a_store_erase_failure);
    RUN_TEST(test_the_store_hands_back_a_failed_erase_transfer);
    RUN_TEST(test_the_store_hands_back_a_failed_write_transfer);
    RUN_TEST(test_the_store_hands_back_a_failed_read_transfer);
    RUN_TEST(test_the_store_refuses_more_than_a_chunk);
    RUN_TEST(test_a_part_that_stays_busy_times_the_erase_out);
    return UNITY_END();
}
