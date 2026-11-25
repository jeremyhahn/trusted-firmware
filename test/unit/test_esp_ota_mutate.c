/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 OTA session, src/hal/esp32/hal_ota.c.
 *
 * test_esp_ota walks each command's accept and refuse paths. This holds the
 * edges a fault moves and that walk does not reach:
 *
 *   - OTA_BEGIN's length at one past the trailer and at the slot's size, and
 *     a refused esp_ota_begin with a session in flight (one abort, no stale
 *     handle);
 *   - OTA_BEGIN's per-boot budget: spent by a failed esp_ota_begin, not by a
 *     refusal before the erase, and refilled by a successful activate;
 *   - OTA_DATA's last byte exactly, and one past it;
 *   - the activate check's image end at each bound of its range (the
 *     digest, the slot, the trailer's room), a slot-filling image, an
 *     unreadable digest, description or trailer, an image with no appended
 *     SHA-256, and a measurement wrong in its last byte only;
 *   - the trailer outside the uploaded stream: a session that sent no
 *     trailer, with an earlier session's left on the slot past its digest;
 *   - the uploaded stream's bound on each side: a session one byte short of
 *     the whole trailer, and one stopped before the digest over an earlier
 *     session's whole image;
 *   - a repeated ACTIVATE with no new OTA_AUTH: the mode the install was
 *     activated with is OK, another mode is BAD_STATE, and neither writes;
 *   - activate and install_pending with a session still receiving;
 *   - the activate record's reserved bytes, zero over a dirty stack;
 *   - OTA_INFO when either slot's description or digest cannot be read.
 *
 * The image end is poked into the mock's flash after the image is ended,
 * where ESP-IDF's metadata reads it. Deterministic: literal lengths only.
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

#define CHUNK     60u      /* TFW_MAX_PAYLOAD less the offset */
#define INFO_LEN  44u
#define APP_LEN   1500u    /* ESP-IDF's image, its digest the last 32 bytes */
#define DESC_AT   0x20u    /* the app description's magic word */

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

/* ESP-IDF's image of app bytes, digest appended, then trailer v1 naming
 * that digest and its span, as test_esp_ota builds it. */
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

static tfw_status_t dispatch(uint8_t cmd, const uint8_t *in, uint8_t n, uint8_t *out, uint8_t cap)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, cap);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t authorized_activate(uint8_t mode)
{
    uint8_t out[8];

    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_AUTH, sig, (uint8_t)sizeof sig, out, sizeof out));
    return dispatch(TFW_CMD_OTA_ACTIVATE, &mode, 1u, out, sizeof out);
}

static void expect_nothing_activated(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL(ESP_OTA_IMG_UNDEFINED, mock_esp.state[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sys_reset_pending());
}

/* A session left receiving is abandoned while its handle is held, so the
 * next test's init does not abort a handle the mock has already reset. */
static void abandon(void)
{
    tfw_stage_init();
    TEST_ASSERT_FALSE(mock_esp.open);
}

/* A signed image staged whole, then ESP-IDF's image end moved to end_at. */
static tfw_status_t activate_with_end(uint32_t end_at)
{
    stage_signed(2u);
    put32(&mock_esp.flash[1][MOCK_ESP_O_END], end_at);
    return authorized_activate(0u);
}

/* ------------------------------------------------------------- OTA_BEGIN */

static void test_begin_takes_one_past_the_trailer_and_the_whole_slot(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(TFW_BOOT_TRAILER_LEN));
    TEST_ASSERT_EQUAL(TFW_OK, begin(TFW_BOOT_TRAILER_LEN + 1u));
    TEST_ASSERT_EQUAL(TFW_OK, begin(MOCK_ESP_SLOT_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(MOCK_ESP_SLOT_LEN + 1u));
    TEST_ASSERT_EQUAL_UINT(2u, mock_esp.begins);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    abandon();
}

static void test_a_refused_begin_over_a_session_aborts_its_handle_once(void)
{
    mock_esp_image(img, APP_LEN, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    stream(0u, CHUNK);
    mock_esp.begin_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, begin(APP_LEN));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, data(CHUNK, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, end());
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.aborts);
}

/* The per-boot budget is spent by the erase, whether or not it succeeds: a
 * failing esp_ota_begin cannot be retried without bound. */
static void test_a_failed_esp_ota_begin_spends_the_budget(void)
{
    uint32_t i, before;

    mock_esp.begin_err = ESP_FAIL;
    for (i = 0u; i < (uint32_t)TFW_OTA_BEGIN_BUDGET; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, begin(APP_LEN));
    }
    mock_esp.begin_err = ESP_OK;
    before = mock_esp.begins;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(APP_LEN));
    TEST_ASSERT_EQUAL_UINT(before, mock_esp.begins);   /* refused before the erase */
    tfw_stage_init();   /* a reset */
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    abandon();
}

/* A BEGIN refused before the erase (a bad length, no update slot) is free:
 * malformed BEGINs do not lock out the real upload. */
static void test_a_begin_refused_before_the_erase_spends_none(void)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)TFW_OTA_BEGIN_BUDGET + 1u; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(0u));
    }
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, begin(MOCK_ESP_SLOT_LEN + 1u));
    mock_esp.no_next = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, begin(APP_LEN));
    mock_esp.no_next = 0u;
    for (i = 0u; i < (uint32_t)TFW_OTA_BEGIN_BUDGET; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    }
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, begin(APP_LEN));
    abandon();
}

/* A successful activate refills the budget. */
static void test_a_successful_activate_refills_the_budget(void)
{
    uint32_t i;

    mock_esp.begin_err = ESP_FAIL;
    for (i = 0u; i + 1u < (uint32_t)TFW_OTA_BEGIN_BUDGET; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, begin(APP_LEN));
    }
    mock_esp.begin_err = ESP_OK;
    stage_signed(2u);   /* spends the last one */
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));   /* mode 0: no reset */
    /* The test's stand-in for the install no longer pending: otadata names
     * the running slot again. The budget is what is tested here. */
    mock_esp.boot_slot = (uint8_t)mock_esp.running;
    for (i = 0u; i < (uint32_t)TFW_OTA_BEGIN_BUDGET; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    }
    abandon();
}

/* -------------------------------------------------------------- OTA_DATA */

static void test_data_takes_the_last_byte_exactly(void)
{
    mock_esp_image(img, 200u, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(200u));
    stream(0u, 180u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(180u, 21u));
    TEST_ASSERT_EQUAL(TFW_OK, data(180u, 20u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, data(200u, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, end());
}

/* ------------------------------------------------- the activate check */

static void test_an_image_end_at_the_digest_alone_is_range(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, activate_with_end(TFW_BOOT_MEASURE_LEN));
    expect_nothing_activated();
}

static void test_an_image_end_one_past_the_digest_is_not_range(void)
{
    const tfw_status_t st = activate_with_end(TFW_BOOT_MEASURE_LEN + 1u);

    TEST_ASSERT_NOT_EQUAL(TFW_OK, st);
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_RANGE, st);
    expect_nothing_activated();
}

static void test_an_image_end_past_the_slot_is_range(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, activate_with_end(MOCK_ESP_SLOT_LEN + 1u));
    expect_nothing_activated();
}

static void test_an_image_end_with_no_room_for_the_trailer_is_range(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      activate_with_end(MOCK_ESP_SLOT_LEN - TFW_BOOT_TRAILER_LEN + 1u));
    expect_nothing_activated();
}

static void test_an_image_whose_trailer_fills_the_slot_activates(void)
{
    const uint32_t app = MOCK_ESP_SLOT_LEN - TFW_BOOT_TRAILER_LEN;

    signed_image(app, 2u, 2u);
    stage_img(app);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.set_boots);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_esp.boot_slot);
}

static void __attribute__((noinline)) dirty_stack(void)
{
    volatile uint8_t junk[8192];

    for (unsigned i = 0u; i < sizeof junk; i++) {
        junk[i] = 0xAAu;
    }
}

/* The activate record is built on the stack, and boot.h gives bytes 5-7 as
 * 0: a record not cleared first carries the 0xAA left there. */
static void test_the_activate_record_reserved_bytes_are_zero(void)
{
    stage_signed(2u);
    dirty_stack();
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.auth_writes);
    for (unsigned i = TFW_BOOT_AUTH_O_MODE + 1u; i < TFW_BOOT_AUTH_O_SIG; i++) {
        TEST_ASSERT_EQUAL_UINT8(0u, mock_esp.auth[i]);
    }
}

/* A session that uploads the image and its digest but no trailer: an earlier
 * session's trailer still sits on the slot just past the digest, so only the
 * uploaded length can tell the check it is not this session's. */
static void test_a_trailer_past_the_uploaded_stream_is_range(void)
{
    signed_image(APP_LEN, 2u, 2u);
    memcpy(&mock_esp.flash[1][APP_LEN], &img[APP_LEN], TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    stream(0u, APP_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
}

/* An earlier session's whole trailer is on the slot past the image; this
 * session sent the image and all of the trailer but its last byte. */
static void test_a_trailer_uploaded_one_byte_short_is_range(void)
{
    signed_image(APP_LEN, 2u, 2u);
    memcpy(&mock_esp.flash[1][APP_LEN], &img[APP_LEN], TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN + TFW_BOOT_TRAILER_LEN - 1u));
    stream(0u, APP_LEN + TFW_BOOT_TRAILER_LEN - 1u);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
}

/* This session stopped before the digest's end of the image ESP-IDF's
 * metadata reports: the uploaded length is less than that image's, and
 * total - image_len must not wrap past the trailer's bound. */
static void test_an_upload_that_stops_before_the_digest_is_range(void)
{
    const uint32_t sent = APP_LEN - TFW_BOOT_MEASURE_LEN;

    /* esp_ota_end refuses an image that ends past the bytes written, so
     * this session's stream is a whole ESP-IDF image of its own. */
    mock_esp_image(img, sent, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(sent));
    stream(0u, sent);
    TEST_ASSERT_EQUAL(TFW_OK, end());
    /* Past it, an earlier session's image tail and whole trailer; ESP-IDF's
     * image end, poked after the end as above, is that image's. */
    signed_image(APP_LEN, 2u, 2u);
    memcpy(&mock_esp.flash[1][sent], &img[sent], APP_LEN + TFW_BOOT_TRAILER_LEN - sent);
    put32(&mock_esp.flash[1][MOCK_ESP_O_END], APP_LEN);
    /* ESP-IDF's partition digest reads the flash, not the session's count;
     * the mock's written length stands in for it, so it spans that image. */
    mock_esp.len[1] = APP_LEN + TFW_BOOT_TRAILER_LEN;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
}

static void test_an_unreadable_digest_is_internal(void)
{
    stage_signed(2u);
    mock_esp.len[1] = APP_LEN - 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(0u));
    expect_nothing_activated();
}

static void test_an_unreadable_description_is_internal(void)
{
    stage_signed(2u);
    mock_esp.flash[1][DESC_AT] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(0u));
    expect_nothing_activated();
}

static void test_an_image_with_no_appended_digest_is_range(void)
{
    stage_signed(2u);
    mock_esp.no_hash = 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, authorized_activate(0u));
    expect_nothing_activated();
    mock_esp.no_hash = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(0u));
}

static void test_an_unreadable_trailer_is_internal(void)
{
    stage_signed(2u);
    mock_esp.read_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, authorized_activate(0u));
    expect_nothing_activated();
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.auth_writes);
}

static void test_a_measurement_wrong_in_its_last_byte_is_crypto(void)
{
    signed_image(APP_LEN, 2u, 2u);
    img[APP_LEN + TFW_BOOT_TR_MEASUREMENT + TFW_BOOT_MEASURE_LEN - 1u] ^= 0x01u;
    stage_img(APP_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, authorized_activate(0u));
    expect_nothing_activated();
}

/* --------------------------------------------- a session still receiving */

/* A host whose ACTIVATE reply was lost asks again: the same mode, with no new
 * OTA_AUTH, is the install already done; another mode is not. Neither writes. */
static void test_a_repeated_activate_answers_by_the_mode_it_was_given(void)
{
    uint8_t out[8];
    uint8_t mode;

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, authorized_activate(1u));
    mode = 1u;
    TEST_ASSERT_EQUAL(TFW_OK, dispatch(TFW_CMD_OTA_ACTIVATE, &mode, 1u, out, sizeof out));
    mode = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, dispatch(TFW_CMD_OTA_ACTIVATE, &mode, 1u, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.auth_writes);
    TEST_ASSERT_EQUAL_UINT(1u, mock_esp.set_boots);
}

static void test_activate_while_receiving_is_bad_state(void)
{
    mock_esp_image(img, APP_LEN, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    stream(0u, CHUNK);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_stage_activate());
    TEST_ASSERT_EQUAL_UINT(0u, mock_esp.set_boots);
    abandon();
}

static void test_no_install_is_pending_while_receiving(void)
{
    mock_esp_image(img, APP_LEN, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, begin(APP_LEN));
    stream(0u, CHUNK);
    mock_esp.boot_slot = 1u;   /* otadata already names the slot */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    abandon();
}

/* -------------------------------------------------------------- OTA_INFO */

static void test_info_on_a_running_slot_with_no_description_is_internal(void)
{
    uint8_t out[INFO_LEN];

    mock_esp.flash[0][DESC_AT] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(0u, out));
    mock_esp.flash[0][DESC_AT] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
}

static void test_info_on_a_running_slot_with_no_digest_is_internal(void)
{
    uint8_t        out[INFO_LEN];
    const uint32_t len = mock_esp.len[0];

    mock_esp.len[0] = 32u + 1u;   /* the description reads, the digest does not */
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(0u, out));
    mock_esp.len[0] = len;
    TEST_ASSERT_EQUAL(TFW_OK, info(0u, out));
}

static void test_info_on_a_staged_slot_with_no_description_is_internal(void)
{
    uint8_t out[INFO_LEN];

    stage_signed(2u);
    TEST_ASSERT_EQUAL(TFW_OK, info(1u, out));
    mock_esp.flash[1][DESC_AT] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, info(1u, out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_takes_one_past_the_trailer_and_the_whole_slot);
    RUN_TEST(test_a_refused_begin_over_a_session_aborts_its_handle_once);
    RUN_TEST(test_a_failed_esp_ota_begin_spends_the_budget);
    RUN_TEST(test_a_begin_refused_before_the_erase_spends_none);
    RUN_TEST(test_a_successful_activate_refills_the_budget);
    RUN_TEST(test_data_takes_the_last_byte_exactly);
    RUN_TEST(test_an_image_end_at_the_digest_alone_is_range);
    RUN_TEST(test_an_image_end_one_past_the_digest_is_not_range);
    RUN_TEST(test_an_image_end_past_the_slot_is_range);
    RUN_TEST(test_an_image_end_with_no_room_for_the_trailer_is_range);
    RUN_TEST(test_an_image_whose_trailer_fills_the_slot_activates);
    RUN_TEST(test_the_activate_record_reserved_bytes_are_zero);
    RUN_TEST(test_a_trailer_past_the_uploaded_stream_is_range);
    RUN_TEST(test_a_trailer_uploaded_one_byte_short_is_range);
    RUN_TEST(test_an_upload_that_stops_before_the_digest_is_range);
    RUN_TEST(test_an_unreadable_digest_is_internal);
    RUN_TEST(test_an_unreadable_description_is_internal);
    RUN_TEST(test_an_image_with_no_appended_digest_is_range);
    RUN_TEST(test_an_unreadable_trailer_is_internal);
    RUN_TEST(test_a_measurement_wrong_in_its_last_byte_is_crypto);
    RUN_TEST(test_a_repeated_activate_answers_by_the_mode_it_was_given);
    RUN_TEST(test_activate_while_receiving_is_bad_state);
    RUN_TEST(test_no_install_is_pending_while_receiving);
    RUN_TEST(test_info_on_a_running_slot_with_no_description_is_internal);
    RUN_TEST(test_info_on_a_running_slot_with_no_digest_is_internal);
    RUN_TEST(test_info_on_a_staged_slot_with_no_description_is_internal);
    return UNITY_END();
}
