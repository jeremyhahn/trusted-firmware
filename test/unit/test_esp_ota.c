/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 OTA session (issue #48, slices 4 and 5): src/hal/esp32/hal_ota.c
 * against test/mock/esp_ota_mock.c, linked with the gates an ESP32 image
 * carries (TFW_FEATURE_OTA_STAGE, _OTA_INFO, _OTA_ACTIVATE and _OTA_CTL) and
 * ESP-IDF's anti-rollback on, as the esp32c3 QEMU board builds. Every case also
 * asserts that no call reached a handle already given back, that the running
 * image was never written, and that no boot partition was set without the
 * tfw_auth record layer 0 verifies being written first.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "esp_ota_mock.h"
#include "esp_image_format.h"
#include "ota_stage.h"
#include "ota_ctl.h"
#include "ota_auth.h"
#include "ota_trial.h"
#include "dispatch.h"
#include "sys.h"

#define CHUNK    60u      /* TFW_MAX_PAYLOAD less the offset */
#define INFO_LEN 44u
#define APP_LEN  1500u    /* ESP-IDF's image, its digest the last 32 bytes */

static uint8_t img[MOCK_ESP_SLOT_LEN];
static uint8_t running[MOCK_ESP_SLOT_LEN];
static uint8_t sig[TFW_OTA_AUTH_SIG_LEN];

void setUp(void)
{
    mock_esp_reset();
    tfw_stage_init();
    tfw_sys_reset_state();
    tfw_dispatch_refused(TFW_CMD_OTA_STATUS);   /* no OTA_AUTH just before */
    memcpy(running, mock_esp.flash[0], sizeof running);
    memset(sig, 0xA5, sizeof sig);
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.stale);
    TEST_ASSERT_TRUE(mock_esp.set_boots <= mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_MEMORY(running, mock_esp.flash[mock_esp.running], sizeof running);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n, uint8_t *out, uint8_t cap)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, cap);
    return fn(&r, &w);
}

static tfw_status_t begin(uint32_t total)
{
    uint8_t in[4], out[8];

    put32(in, total);
    return call(tfw_stage_cmd_begin, in, 4u, out, sizeof out);
}

static tfw_status_t data(uint32_t off, uint8_t n)
{
    uint8_t in[4u + CHUNK], out[8];

    put32(in, off);
    memcpy(&in[4], &img[off], n);
    return call(tfw_stage_cmd_data, in, (uint8_t)(4u + n), out, sizeof out);
}

static tfw_status_t end(void)
{
    uint8_t out[8];

    return call(tfw_stage_cmd_end, NULL, 0u, out, sizeof out);
}

static void expect_status(uint8_t state, uint32_t received)
{
    uint8_t out[8];

    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_status, NULL, 0u, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8(state, out[0]);
    TEST_ASSERT_EQUAL_UINT32(received, get32(&out[1]));
}

static void stream(uint32_t from, uint32_t to)
{
    uint32_t off;
    uint8_t  n;

    for (off = from; off < to; off += n) {
        n = (uint8_t)(to - off < CHUNK ? to - off : CHUNK);
        TEST_ASSERT_EQUAL(TFW_OK, data(off, n));
    }
}

static tfw_status_t info(uint8_t which, uint8_t out[INFO_LEN])
{
    return call(tfw_otactl_cmd_info, &which, 1u, out, INFO_LEN);
}

/* A good image of len bytes, streamed whole and ended. */
static void stage(uint32_t len, uint32_t secure_version)
{
    mock_esp_image(img, len, secure_version);
    TEST_ASSERT_EQUAL(TFW_OK, begin(len));
    stream(0u, len);
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

/* The layout the build signs (mk/arch/esp32-targets.mk): ESP-IDF's image of
 * app bytes, digest appended, then trailer v1 naming that digest and its span.
 * secure_version is the app description's, version the trailer's; the build
 * stamps one from the other, a test may not. */
static void signed_image(uint32_t app, uint32_t secure_version, uint32_t version)
{
    uint8_t *t = &img[app];

    mock_esp_image(img, app, secure_version);
    memset(t, 0, TFW_BOOT_TRAILER_LEN);
    put32(&t[TFW_BOOT_TR_MAGIC], TFW_BOOT_TRAILER_MAGIC);
    t[TFW_BOOT_TR_VERSION]     = (uint8_t)TFW_BOOT_TRAILER_VERSION;
    t[TFW_BOOT_TR_BOARD_ID]    = (uint8_t)TFW_BOARD_ID;
    t[TFW_BOOT_TR_IMAGE_FLAGS] = 0u;
    put32(&t[TFW_BOOT_TR_IMAGE_VERSION], version);
    put32(&t[TFW_BOOT_TR_IMAGE_LEN], app - TFW_BOOT_MEASURE_LEN);
    memcpy(&t[TFW_BOOT_TR_MEASUREMENT], &img[app - TFW_BOOT_MEASURE_LEN], TFW_BOOT_MEASURE_LEN);
    memset(&t[TFW_BOOT_TR_SIGNATURE], 0x5A, TFW_BOOT_SIG_LEN);   /* layer 0's to verify */
}

/* img as it stands, streamed whole and ended. */
static void stage_img(uint32_t app)
{
    TEST_ASSERT_EQUAL(TFW_OK, begin(app + TFW_BOOT_TRAILER_LEN));
    stream(0u, app + TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void stage_signed(uint32_t version)
{
    signed_image(APP_LEN, version, version);
    stage_img(APP_LEN);
}

/* ------------------------------------------------------------ accept paths */

static void test_an_image_is_written_into_the_slot_not_running(void)
{
    mock_esp_image(img, 1500u, 7u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.begins);
    TEST_ASSERT_EQUAL_UINT32(OTA_WITH_SEQUENTIAL_WRITES, (uint32_t)mock_esp.begin_size);
    expect_status(TFW_STAGE_RECEIVING, 0u);
    stream(0u, 720u);
    expect_status(TFW_STAGE_RECEIVING, 720u);
    stream(720u, 1500u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    expect_status(TFW_STAGE_COMPLETE, 1500u);
    TEST_ASSERT_EQUAL_MEMORY(img, mock_esp.flash[1], 1500u);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.aborts);
}

static void test_running_from_slot_1_writes_slot_0(void)
{
    mock_esp.running  = 1u;
    mock_esp.len[1]   = 1024u;
    mock_esp_image(mock_esp.flash[1], 1024u, 2u);
    memcpy(running, mock_esp.flash[1], sizeof running);   /* tearDown's check */
    stage(800u, 3u);
    TEST_ASSERT_EQUAL_MEMORY(img, mock_esp.flash[0], 800u);
}

static void test_a_slot_sized_image_fills_the_slot(void)
{
    stage(MOCK_ESP_SLOT_LEN, 1u);
    expect_status(TFW_STAGE_COMPLETE, MOCK_ESP_SLOT_LEN);
    TEST_ASSERT_EQUAL_MEMORY(img, mock_esp.flash[1], MOCK_ESP_SLOT_LEN);
}

static void test_info_reports_the_running_slot_and_the_session(void)
{
    uint8_t out[INFO_LEN];

    mock_esp.secure_version = 3u;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(2u, out[0]);                       /* slots */
    TEST_ASSERT_EQUAL_UINT8(0u, out[1]);                       /* active */
    TEST_ASSERT_EQUAL_UINT8(0u, out[2]);                       /* flags */
    TEST_ASSERT_EQUAL_UINT32(3u, get32(&out[3]));              /* floor: the eFuse */
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, out[7]);
    TEST_ASSERT_EQUAL_UINT32(1u, get32(&out[8]));              /* secure_version */
    TEST_ASSERT_EQUAL_MEMORY(&running[1024u - 32u], &out[12], 32u);

    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_IDLE, out[7]);
    TEST_ASSERT_EQUAL_UINT32(0u, get32(&out[8]));
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, &out[12], 32u);

    stage(1500u, 9u);
    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_COMPLETE, out[7]);
    TEST_ASSERT_EQUAL_UINT32(9u, get32(&out[8]));
    TEST_ASSERT_EQUAL_MEMORY(&img[1500u - 32u], &out[12], 32u);
    TEST_ASSERT_EQUAL_UINT32(3u, get32(&out[3]));
}

static void test_info_reports_a_trial_and_a_fall_back(void)
{
    uint8_t out[INFO_LEN];

    mock_esp.state[0] = ESP_OTA_IMG_PENDING_VERIFY;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(0x04u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, out[7]);

    mock_esp.state[0] = ESP_OTA_IMG_VALID;
    mock_esp.state[1] = ESP_OTA_IMG_ABORTED;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, out[7]);

    mock_esp.state[1] = ESP_OTA_IMG_INVALID;
    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[2]);
}

/* ---------------------------------------------------------------- refusals */

static void test_begin_is_refused_while_the_running_image_is_on_trial(void)
{
    mock_esp.state[0] = ESP_OTA_IMG_PENDING_VERIFY;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.begins);
    expect_status(TFW_STAGE_IDLE, 0u);
}

static void test_begin_is_refused_by_esp_ota_on_a_rollback_state(void)
{
    mock_esp.begin_err = ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(1500u));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
}

static void test_begin_refuses_a_length_outside_the_slot(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(TFW_BOOT_TRAILER_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(MOCK_ESP_SLOT_LEN + 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.begins);
    expect_status(TFW_STAGE_IDLE, 0u);
}

static void test_begin_with_no_update_slot_is_unsupported(void)
{
    mock_esp.no_next = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.begins);
}

static void test_a_failing_esp_ota_begin_fails_the_session(void)
{
    mock_esp.begin_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, begin(1500u));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(0u, CHUNK));
}

static void test_data_and_end_need_a_session(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(0u, CHUNK));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.writes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.ends);
}

static void test_an_out_of_order_offset_is_refused(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(CHUNK, CHUNK));       /* ahead */
    TEST_ASSERT_EQUAL(TFW_OK, data(0u, CHUNK));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(0u, CHUNK));          /* again */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(CHUNK + 1u, CHUNK));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.writes);
    expect_status(TFW_STAGE_RECEIVING, CHUNK);                  /* refused, not failed */
    stream(CHUNK, 1500u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void test_data_past_the_announced_length_is_refused(void)
{
    mock_esp_image(img, 200u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(200u));
    stream(0u, 180u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(180u, CHUNK));        /* 20 left */
    TEST_ASSERT_EQUAL_UINT(3u, mock_esp.writes);
    expect_status(TFW_STAGE_RECEIVING, 180u);
    TEST_ASSERT_EQUAL(TFW_OK, data(180u, 20u));
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void test_a_write_past_the_slot_fails_the_session(void)
{
    /* esp_ota's own bound, should the announced one ever be wider. */
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    mock_esp.write_err = ESP_ERR_INVALID_SIZE;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(0u, CHUNK));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
}

static void test_a_failing_write_fails_the_session(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL(TFW_OK, data(0u, CHUNK));
    mock_esp.write_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, data(CHUNK, CHUNK));
    expect_status(TFW_STAGE_FAILED, CHUNK);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
}

static void test_a_first_byte_that_is_not_the_magic_fails_at_data(void)
{
    mock_esp_image(img, 1500u, 1u);
    img[0] = 0x00u;
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, data(0u, CHUNK));
    expect_status(TFW_STAGE_FAILED, 0u);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
}

static void test_a_short_image_fails_at_end(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    stream(0u, 1440u);
    TEST_ASSERT_EQUAL(TFW_ERR_FRAMING, end());
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.ends);                 /* never checked */
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
    expect_status(TFW_STAGE_FAILED, 1440u);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(1440u, CHUNK));
}

static void test_a_corrupted_image_fails_at_end(void)
{
    uint8_t out[INFO_LEN];

    mock_esp_image(img, 1500u, 4u);
    img[700] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    stream(0u, 1500u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, end());
    expect_status(TFW_STAGE_FAILED, 1500u);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.ends);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.aborts);               /* end gave it back */
    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_FAILED, out[7]);
    TEST_ASSERT_EQUAL_UINT32(0u, get32(&out[8]));
    TEST_ASSERT_EACH_EQUAL_UINT8(0u, &out[12], 32u);
}

static void test_a_failing_esp_ota_end_maps_its_code(void)
{
    static const struct { esp_err_t err; tfw_status_t st; } cases[] = {
        { ESP_ERR_OTA_VALIDATE_FAILED,        TFW_ERR_CRYPTO },
        { ESP_ERR_OTA_ROLLBACK_INVALID_STATE, TFW_ERR_BAD_STATE },
        { ESP_ERR_INVALID_SIZE,               TFW_ERR_RANGE },
        { ESP_FAIL,                           TFW_ERR_INTERNAL },
        { ESP_ERR_NOT_FOUND,                  TFW_ERR_INTERNAL },
    };
    size_t i;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        mock_esp.end_err = ESP_OK;
        mock_esp_image(img, 300u, 1u);
        tfw_stage_init();   /* each case a boot: the OTA_BEGIN budget is per boot (#232) */
        TEST_ASSERT_EQUAL(TFW_OK, begin(300u));
        stream(0u, 300u);
        mock_esp.end_err = cases[i].err;
        TEST_ASSERT_EQUAL(cases[i].st, end());
        expect_status(TFW_STAGE_FAILED, 300u);
        TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
    }
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.aborts);
}

static void test_a_new_begin_abandons_the_session_in_flight(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    stream(0u, 600u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL_UINT(2u, mock_esp.begins);
    expect_status(TFW_STAGE_RECEIVING, 0u);
    stream(0u, 1500u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void test_a_refused_begin_keeps_the_session_in_flight(void)
{
    mock_esp_image(img, 1500u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    stream(0u, 600u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(0u));
    expect_status(TFW_STAGE_RECEIVING, 600u);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.aborts);
    stream(600u, 1500u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

static void test_init_gives_an_open_handle_back(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    tfw_stage_init();
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.open);
    expect_status(TFW_STAGE_IDLE, 0u);
}

static void test_info_refuses_a_third_slot_and_an_unreadable_one(void)
{
    uint8_t out[INFO_LEN];

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, info(2u, out));
    mock_esp.len[0] = 0u;                                       /* no description */
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(0u, out));
    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));                  /* the session needs none */
}

/* -------------------------------------------------- through the dispatcher */

static tfw_status_t dispatch(uint8_t cmd, const uint8_t *in, uint8_t n, uint8_t *out, uint8_t cap)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, cap);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t auth(void)
{
    uint8_t out[8];

    return dispatch(TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, out, sizeof out);
}

static tfw_status_t activate(uint8_t mode)
{
    uint8_t out[8];

    return dispatch(TFW_CMD_OTA_ACTIVATE, &mode, 1u, out, sizeof out);
}

static tfw_status_t confirm(void)
{
    uint8_t out[8];

    return dispatch(TFW_CMD_OTA_CONFIRM, NULL, 0u, out, sizeof out);
}

static tfw_status_t authorized_activate(uint8_t mode)
{
    TEST_ASSERT_EQUAL(TFW_OK, auth());
    return activate(mode);
}

static tfw_status_t authorized_confirm(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, auth());
    return confirm();
}

/* The reset after an activate: ESP-IDF's bootloader boots the NEW slot once,
 * as PENDING_VERIFY, and this image starts over. */
static void boot_the_trial(void)
{
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_NEW, mock_esp.state[mock_esp.boot_slot]);
    mock_esp.running                    = mock_esp.boot_slot;
    mock_esp.state[mock_esp.boot_slot]  = ESP_OTA_IMG_PENDING_VERIFY;
    memcpy(running, mock_esp.flash[mock_esp.running], sizeof running);
    tfw_stage_init();
    tfw_sys_reset_state();
}

static void expect_nothing_activated(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_UNDEFINED, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
}

/* ---------------------------------------------------- OTA_AUTH, OTA_ACTIVATE */

static void test_activate_writes_the_record_then_sets_the_boot_slot(void)
{
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_AUTH_MAGIC, get32(&mock_esp.auth[TFW_BOOT_AUTH_O_MAGIC]));
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.auth[TFW_BOOT_AUTH_O_MODE]);
    TEST_ASSERT_EQUAL_MEMORY(sig, &mock_esp.auth[TFW_BOOT_AUTH_O_SIG], sizeof sig);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_esp.boot_slot);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_NEW, mock_esp.state[1]);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_VALID, mock_esp.state[0]);   /* still running */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());       /* mode 0: the host resets */
}

/* RESET asks whether its reset installs (#51): only after this session's
 * OTA_ACTIVATE, and no longer once the session starts over. */
static void test_install_pending_follows_activate(void)
{
    stage_signed(2u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_stage_install_pending());
    tfw_stage_init();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
}

/* #232: a BEGIN must not wipe an activation waiting for its reset, since
 * esp_ota_begin erases the very slot set_boot named. */
static void test_begin_is_refused_while_an_activation_is_pending(void)
{
    static uint8_t staged[MOCK_ESP_SLOT_LEN];
    unsigned       begins, writes, aborts;
    uint32_t       len;

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    memcpy(staged, mock_esp.flash[1], sizeof staged);
    len    = mock_esp.len[1];
    begins = mock_esp.begins;
    writes = mock_esp.writes;
    aborts = mock_esp.aborts;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(begins, mock_esp.begins);
    TEST_ASSERT_EQUAL_UINT(writes, mock_esp.writes);
    TEST_ASSERT_EQUAL_UINT(aborts, mock_esp.aborts);
    TEST_ASSERT_EQUAL_MEMORY(staged, mock_esp.flash[1], sizeof staged);
    TEST_ASSERT_EQUAL_UINT32(len, mock_esp.len[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_stage_install_pending());
    expect_status(TFW_STAGE_COMPLETE, len);
}

static void test_begin_over_a_complete_image_not_activated_starts_over(void)
{
    unsigned begins;

    stage_signed(2u);
    begins = mock_esp.begins;
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(begins + 1u, mock_esp.begins);
    expect_status(TFW_STAGE_RECEIVING, 0u);
    tfw_stage_init();   /* give the handle back before setUp drops the mock's */
}

static void test_begin_works_again_after_the_reset(void)
{
    unsigned begins;

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(1500u));
    tfw_stage_init();
    tfw_sys_reset_state();
    begins = mock_esp.begins;
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(begins + 1u, mock_esp.begins);
    expect_status(TFW_STAGE_RECEIVING, 0u);
    tfw_stage_init();   /* give the handle back before setUp drops the mock's */
}

/* #232 (B): TFW_OTA_BEGIN_BUDGET stages a boot, then BAD_STATE before
 * esp_ota_begin erases; the session in flight is kept and a reset refills. */
static void test_begin_past_the_budget_is_refused_until_reset(void)
{
    unsigned begins, aborts, i;

    TEST_ASSERT_EQUAL_UINT(4u, TFW_OTA_BEGIN_BUDGET);
    for (i = 0u; i < (unsigned)TFW_OTA_BEGIN_BUDGET; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    }
    begins = mock_esp.begins;
    aborts = mock_esp.aborts;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(begins, mock_esp.begins);
    TEST_ASSERT_EQUAL_UINT(aborts, mock_esp.aborts);
    expect_status(TFW_STAGE_RECEIVING, 0u);
    tfw_stage_init();
    TEST_ASSERT_EQUAL(TFW_OK, begin(1500u));
    TEST_ASSERT_EQUAL_UINT(begins + 1u, mock_esp.begins);
    tfw_stage_init();   /* give the handle back before setUp drops the mock's */
}

/* m1100 (3), m1117 (1): while an activation is pending, the same exchange
 * again, with no OTA_AUTH since and the same mode byte, is a retry that
 * answers success and changes nothing; one after a new OTA_AUTH, whatever
 * its signature, is a new attempt and is refused, as is another mode. */
static void test_a_second_activate_while_pending_is_a_retry_or_refused(void)
{
    unsigned writes, boots;

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    writes = mock_esp.auth_writes;
    boots  = mock_esp.set_boots;
    TEST_ASSERT_EQUAL(TFW_OK, activate(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, activate(1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, activate(0u));   /* the OTA_AUTH arrived */
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(1u));
    TEST_ASSERT_EQUAL_UINT(writes, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(boots, mock_esp.set_boots);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_stage_install_pending());
}

static void test_activate_mode_1_resets_once_the_reply_is_out(void)
{
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(1u));
    TEST_ASSERT_EQUAL_UINT8(1u, mock_esp.auth[TFW_BOOT_AUTH_O_MODE]);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.set_boots);
}

static void test_activate_from_slot_1_names_slot_0(void)
{
    mock_esp.running   = 1u;
    mock_esp.boot_slot = 1u;   /* booted from slot 1: slot 0 is not yet activated */
    mock_esp.len[1]    = 1024u;
    mock_esp_image(mock_esp.flash[1], 1024u, 1u);
    mock_esp.state[1] = ESP_OTA_IMG_VALID;
    mock_esp.state[0] = ESP_OTA_IMG_UNDEFINED;
    memcpy(running, mock_esp.flash[1], sizeof running);
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.boot_slot);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_NEW, mock_esp.state[0]);
}

static void test_activate_at_the_floor_passes(void)
{
    mock_esp.secure_version = 2u;
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
}

static void test_activate_needs_an_ota_auth_just_before(void)
{
    uint8_t out[8];

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));       /* none */
    TEST_ASSERT_EQUAL(TFW_OK, auth());
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_STATUS, NULL, 0u, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));       /* one in between */
    TEST_ASSERT_EQUAL(TFW_OK, auth());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_CMD_OTA_AUTH, sig, 63u, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));       /* a short one drops it */
    TEST_ASSERT_EQUAL(TFW_OK, auth());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, dispatch(TFW_CMD_OTA_AUTH, NULL, 0u, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);   /* still activatable */
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
}

static void test_an_authorization_serves_one_command(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));   /* nothing staged */
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, activate(0u));
    expect_nothing_activated();
}

static void test_activate_refuses_a_mode_past_one(void)
{
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0xFFu));
    expect_nothing_activated();
}

static void test_activate_needs_an_ended_image(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));   /* idle */
    signed_image(APP_LEN, 2u, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN + TFW_BOOT_TRAILER_LEN));
    stream(0u, APP_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));   /* receiving */
    stream(APP_LEN, APP_LEN + TFW_BOOT_TRAILER_LEN);
    mock_esp.end_err = ESP_ERR_OTA_VALIDATE_FAILED;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, end());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));   /* failed */
    expect_nothing_activated();
}

static void test_activate_is_refused_on_trial(void)
{
    stage_signed(2u);
    mock_esp.state[0] = ESP_OTA_IMG_PENDING_VERIFY;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(0u));
    expect_nothing_activated();
}

/* Each trailer layer 0 would refuse at the trial boot, refused here first with
 * layer 0's status, so no otadata entry names an image that cannot boot. */
static void test_activate_refuses_a_trailer_layer_0_would_refuse(void)
{
    static const struct { uint8_t at; uint8_t width; uint32_t v; tfw_status_t st; } cases[] = {
        { TFW_BOOT_TR_MAGIC,         4u, 0x4D494655uL,         TFW_ERR_UNSUPPORTED },
        { TFW_BOOT_TR_VERSION,       2u, 2u,                   TFW_ERR_UNSUPPORTED },
        { TFW_BOOT_TR_BOARD_ID,      1u, 0u,                   TFW_ERR_UNSUPPORTED },
        { TFW_BOOT_TR_BOARD_ID,      1u, TFW_BOARD_ID ^ 0x40u, TFW_ERR_UNSUPPORTED },
        { TFW_BOOT_TR_IMAGE_FLAGS,   1u, 0x02u,                TFW_ERR_UNSUPPORTED },
        { TFW_BOOT_TR_IMAGE_VERSION, 4u, 0u,                   TFW_ERR_CRYPTO },
        { TFW_BOOT_TR_IMAGE_LEN,     4u, 0u,                   TFW_ERR_RANGE },
        { TFW_BOOT_TR_IMAGE_LEN,     4u, APP_LEN - 31u,        TFW_ERR_CRYPTO },
        { TFW_BOOT_TR_IMAGE_LEN,     4u, APP_LEN - 33u,        TFW_ERR_CRYPTO },
        { TFW_BOOT_TR_MEASUREMENT,   1u, 0x00u,                TFW_ERR_CRYPTO },
    };
    size_t i;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t *t = &img[APP_LEN + cases[i].at];

        mock_esp_reset();
        tfw_stage_init();
        signed_image(APP_LEN, 2u, 2u);
        if (cases[i].width == 4u) {
            put32(t, cases[i].v);
        } else if (cases[i].width == 2u) {
            t[0] = (uint8_t)cases[i].v;
            t[1] = (uint8_t)(cases[i].v >> 8);
        } else if (cases[i].at == TFW_BOOT_TR_MEASUREMENT) {
            t[0] ^= 0x01u;
        } else {
            t[0] = (uint8_t)cases[i].v;
        }
        stage_img(APP_LEN);
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].st, authorized_activate(0u), "case");
        expect_nothing_activated();
        expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
    }
}

static void test_activate_refuses_an_image_below_the_floor(void)
{
    mock_esp.secure_version = 3u;
    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, authorized_activate(0u));
    expect_nothing_activated();
}

/* Under anti-rollback ESP-IDF's floor is the app description's secure_version
 * and layer 0's is the trailer's: one number, or the image is refused. */
static void test_activate_refuses_a_secure_version_other_than_the_trailers(void)
{
    signed_image(APP_LEN, 3u, 2u);
    stage_img(APP_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, authorized_activate(0u));
    expect_nothing_activated();
}

static void test_activate_refuses_an_image_esp_idf_cannot_walk(void)
{
    stage_signed(2u);
    mock_esp.metadata_err = ESP_ERR_IMAGE_INVALID;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    mock_esp.metadata_err = ESP_ERR_IMAGE_FLASH_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
}

static void test_a_failed_record_write_sets_no_boot_slot(void)
{
    stage_signed(2u);
    mock_esp.auth_err = TFW_ERR_INTERNAL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(1u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_UNDEFINED, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
}

static void test_a_refused_set_boot_is_bad_state_and_no_reset(void)
{
    stage_signed(2u);
    mock_esp.set_boot_err = ESP_ERR_OTA_VALIDATE_FAILED;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_activate(1u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_UNDEFINED, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
}

/* check_staged's walk of what ESP-IDF reports: each refusal below is a slot
 * changed after END, as flash wear or a second writer would leave it. */
static void test_an_image_that_ends_inside_its_digest_is_refused(void)
{
    stage_signed(2u);
    put32(&mock_esp.flash[1][MOCK_ESP_O_END], TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

static void test_an_image_that_ends_past_the_slot_is_refused(void)
{
    stage_signed(2u);
    put32(&mock_esp.flash[1][MOCK_ESP_O_END], (uint32_t)mock_esp_slot(1u)->size + 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

static void test_an_image_with_no_room_for_its_trailer_is_refused(void)
{
    stage_signed(2u);
    put32(&mock_esp.flash[1][MOCK_ESP_O_END],
          (uint32_t)mock_esp_slot(1u)->size - TFW_BOOT_TRAILER_LEN + 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

static void test_a_slot_whose_digest_cannot_be_read_is_internal(void)
{
    stage_signed(2u);
    /* In range with room for a trailer, but past what was written. */
    put32(&mock_esp.flash[1][MOCK_ESP_O_END],
          (uint32_t)mock_esp_slot(1u)->size - TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

static void test_a_slot_with_no_app_description_is_internal(void)
{
    stage_signed(2u);
    mock_esp.flash[1][0x20] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(0u));
    expect_nothing_activated();
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

static void test_stage_activate_with_no_ended_image_sets_no_boot_slot(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_activate());
    expect_nothing_activated();
    expect_status(TFW_STAGE_IDLE, 0u);
}

static void test_info_on_a_running_slot_with_no_digest_is_internal(void)
{
    uint8_t out[INFO_LEN];

    /* A description to read, but too little image for a digest; the running
     * slot's flash itself is left as tearDown expects it. */
    mock_esp.len[0] = TFW_BOOT_MEASURE_LEN;
    memset(out, 0xEE, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(0u, out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xEEu, out, INFO_LEN);
}

static void test_info_on_an_ended_session_with_no_description_is_internal(void)
{
    uint8_t out[INFO_LEN];

    stage_signed(2u);
    mock_esp.flash[1][0x20] ^= 0x01u;
    memset(out, 0xEE, sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(1u, out));
    TEST_ASSERT_EACH_EQUAL_UINT8(0xEEu, out, INFO_LEN);
    expect_status(TFW_STAGE_COMPLETE, APP_LEN + TFW_BOOT_TRAILER_LEN);
}

/* ------------------------------------------------- OTA_CONFIRM and the trial */

static void test_confirm_ends_the_trial(void)
{
    uint8_t out[INFO_LEN];

    stage_signed(4u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    boot_the_trial();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(0x04u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, out[7]);
    TEST_ASSERT_EQUAL_UINT32(4u, get32(&out[8]));

    TEST_ASSERT_EQUAL(TFW_OK, authorized_confirm());
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.marks);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_VALID, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(0u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, out[7]);
    TEST_ASSERT_EQUAL_UINT32(4u, get32(&out[8]));                 /* its secure version */
    TEST_ASSERT_EQUAL_MEMORY(&img[APP_LEN - 32u], &out[12], 32u);
}

static void test_confirm_needs_an_ota_auth_just_before(void)
{
    mock_esp.state[0] = ESP_OTA_IMG_PENDING_VERIFY;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, confirm());
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.marks);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_PENDING_VERIFY, mock_esp.state[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
}

static void test_confirm_off_trial_is_bad_state(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, authorized_confirm());
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.marks);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());
}

static void test_a_failing_confirm_leaves_the_trial_on(void)
{
    mock_esp.state[0] = ESP_OTA_IMG_PENDING_VERIFY;
    mock_esp.mark_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_confirm());
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.marks);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_PENDING_VERIFY, mock_esp.state[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_ota_trial_on());
}

/* A trial layer 0 refused at its one boot is still PENDING_VERIFY while the
 * old image runs again; ESP-IDF marks it ABORTED at the next reset. */
static void test_info_reports_a_refused_trial_as_a_fall_back(void)
{
    uint8_t out[INFO_LEN];

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    mock_esp.state[1] = ESP_OTA_IMG_PENDING_VERIFY;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
    TEST_ASSERT_EQUAL_UINT8(0x01u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, out[7]);
    TEST_ASSERT_EQUAL_UINT32(1u, get32(&out[8]));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_ota_trial_on());
}

static void test_auth_activate_and_confirm_are_on_the_wire(void)
{
    uint8_t zero = 0u, out[INFO_LEN];

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_ACTIVATE, &zero, 1u, out, sizeof out));
    boot_the_trial();
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_INFO, &zero, 1u, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_TRIAL, out[7]);
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_CONFIRM, NULL, 0u, out, sizeof out));
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_VALID, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_esp.running);
}

static void test_the_session_is_on_the_wire(void)
{
    uint8_t in[4], out[INFO_LEN];

    put32(in, 1500u);
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_BEGIN, in, 4u, out, sizeof out));
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_STATUS, NULL, 0u, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_RECEIVING, out[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_image_is_written_into_the_slot_not_running);
    RUN_TEST(test_running_from_slot_1_writes_slot_0);
    RUN_TEST(test_a_slot_sized_image_fills_the_slot);
    RUN_TEST(test_info_reports_the_running_slot_and_the_session);
    RUN_TEST(test_info_reports_a_trial_and_a_fall_back);
    RUN_TEST(test_begin_is_refused_while_the_running_image_is_on_trial);
    RUN_TEST(test_begin_is_refused_by_esp_ota_on_a_rollback_state);
    RUN_TEST(test_begin_refuses_a_length_outside_the_slot);
    RUN_TEST(test_begin_with_no_update_slot_is_unsupported);
    RUN_TEST(test_a_failing_esp_ota_begin_fails_the_session);
    RUN_TEST(test_data_and_end_need_a_session);
    RUN_TEST(test_an_out_of_order_offset_is_refused);
    RUN_TEST(test_data_past_the_announced_length_is_refused);
    RUN_TEST(test_a_write_past_the_slot_fails_the_session);
    RUN_TEST(test_a_failing_write_fails_the_session);
    RUN_TEST(test_a_first_byte_that_is_not_the_magic_fails_at_data);
    RUN_TEST(test_a_short_image_fails_at_end);
    RUN_TEST(test_a_corrupted_image_fails_at_end);
    RUN_TEST(test_a_failing_esp_ota_end_maps_its_code);
    RUN_TEST(test_a_new_begin_abandons_the_session_in_flight);
    RUN_TEST(test_a_refused_begin_keeps_the_session_in_flight);
    RUN_TEST(test_init_gives_an_open_handle_back);
    RUN_TEST(test_info_refuses_a_third_slot_and_an_unreadable_one);
    RUN_TEST(test_activate_writes_the_record_then_sets_the_boot_slot);
    RUN_TEST(test_install_pending_follows_activate);
    RUN_TEST(test_begin_is_refused_while_an_activation_is_pending);
    RUN_TEST(test_begin_over_a_complete_image_not_activated_starts_over);
    RUN_TEST(test_begin_works_again_after_the_reset);
    RUN_TEST(test_begin_past_the_budget_is_refused_until_reset);
    RUN_TEST(test_a_second_activate_while_pending_is_a_retry_or_refused);
    RUN_TEST(test_activate_mode_1_resets_once_the_reply_is_out);
    RUN_TEST(test_activate_from_slot_1_names_slot_0);
    RUN_TEST(test_activate_at_the_floor_passes);
    RUN_TEST(test_activate_needs_an_ota_auth_just_before);
    RUN_TEST(test_an_authorization_serves_one_command);
    RUN_TEST(test_activate_refuses_a_mode_past_one);
    RUN_TEST(test_activate_needs_an_ended_image);
    RUN_TEST(test_activate_is_refused_on_trial);
    RUN_TEST(test_activate_refuses_a_trailer_layer_0_would_refuse);
    RUN_TEST(test_activate_refuses_an_image_below_the_floor);
    RUN_TEST(test_activate_refuses_a_secure_version_other_than_the_trailers);
    RUN_TEST(test_activate_refuses_an_image_esp_idf_cannot_walk);
    RUN_TEST(test_a_failed_record_write_sets_no_boot_slot);
    RUN_TEST(test_a_refused_set_boot_is_bad_state_and_no_reset);
    RUN_TEST(test_an_image_that_ends_inside_its_digest_is_refused);
    RUN_TEST(test_an_image_that_ends_past_the_slot_is_refused);
    RUN_TEST(test_an_image_with_no_room_for_its_trailer_is_refused);
    RUN_TEST(test_a_slot_whose_digest_cannot_be_read_is_internal);
    RUN_TEST(test_a_slot_with_no_app_description_is_internal);
    RUN_TEST(test_stage_activate_with_no_ended_image_sets_no_boot_slot);
    RUN_TEST(test_info_on_a_running_slot_with_no_digest_is_internal);
    RUN_TEST(test_info_on_an_ended_session_with_no_description_is_internal);
    RUN_TEST(test_confirm_ends_the_trial);
    RUN_TEST(test_confirm_needs_an_ota_auth_just_before);
    RUN_TEST(test_confirm_off_trial_is_bad_state);
    RUN_TEST(test_a_failing_confirm_leaves_the_trial_on);
    RUN_TEST(test_info_reports_a_refused_trial_as_a_fall_back);
    RUN_TEST(test_auth_activate_and_confirm_are_on_the_wire);
    RUN_TEST(test_the_session_is_on_the_wire);
    return UNITY_END();
}
