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
 *   - OTA_DATA's last byte exactly, and one past it;
 *   - the activate check's image end at each bound of its range (the
 *     digest, the slot, the trailer's room), a slot-filling image, an
 *     unreadable digest or description, and a measurement wrong in its last
 *     byte only;
 *   - activate and install_pending with a session still receiving;
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

static void test_a_measurement_wrong_in_its_last_byte_is_crypto(void)
{
    signed_image(APP_LEN, 2u, 2u);
    img[APP_LEN + TFW_BOOT_TR_MEASUREMENT + TFW_BOOT_MEASURE_LEN - 1u] ^= 0x01u;
    stage_img(APP_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, authorized_activate(0u));
    expect_nothing_activated();
}

/* --------------------------------------------- a session still receiving */

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
    RUN_TEST(test_data_takes_the_last_byte_exactly);
    RUN_TEST(test_an_image_end_at_the_digest_alone_is_range);
    RUN_TEST(test_an_image_end_one_past_the_digest_is_not_range);
    RUN_TEST(test_an_image_end_past_the_slot_is_range);
    RUN_TEST(test_an_image_end_with_no_room_for_the_trailer_is_range);
    RUN_TEST(test_an_image_whose_trailer_fills_the_slot_activates);
    RUN_TEST(test_an_unreadable_digest_is_internal);
    RUN_TEST(test_an_unreadable_description_is_internal);
    RUN_TEST(test_a_measurement_wrong_in_its_last_byte_is_crypto);
    RUN_TEST(test_activate_while_receiving_is_bad_state);
    RUN_TEST(test_no_install_is_pending_while_receiving);
    RUN_TEST(test_info_on_a_running_slot_with_no_description_is_internal);
    RUN_TEST(test_info_on_a_running_slot_with_no_digest_is_internal);
    RUN_TEST(test_info_on_a_staged_slot_with_no_description_is_internal);
    return UNITY_END();
}
