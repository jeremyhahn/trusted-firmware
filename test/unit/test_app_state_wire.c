/*
 * APP_INFO's first byte is the runner state, on the wire for every state the
 * runner can be in (docs/application-sdk/engineering.md, Error semantics).
 * test_app pins RUNNING and UNSUPPORTED through whole-reply vectors; this pins
 * IDLE, FAILED and STOPPED, and that the descriptor after the state is the
 * same bytes in each. The states are literals on purpose: they extend and
 * never renumber, so a test that read them from the enum would move with it.
 */
#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "vectors.h"

/* State, then the overrun count, two bytes: the descriptor starts here. */
#define DESC_AT 3u

static uint8_t      inbuf[1];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
}

void tearDown(void) {}

static void assert_app_info_state(uint8_t want)
{
    static const uint8_t mock[] = TFWV_APPINFO_OVERRUN_REPLY;

    tfw_reader_init(&r, inbuf, 0u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_dispatch(TFW_CMD_APP_INFO, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(TFWV_APPINFO_OVERRUN_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8(want, outbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0u, outbuf[1]);
    TEST_ASSERT_EQUAL_HEX8(0u, outbuf[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&mock[DESC_AT], &outbuf[DESC_AT],
                                 TFWV_APPINFO_OVERRUN_REPLY_LEN - DESC_AT);
}

static void test_an_application_not_yet_started_is_idle_on_the_wire(void)
{
    assert_app_info_state(0x00u);
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

static void test_a_running_application_is_running_on_the_wire(void)
{
    tfw_app_runner_start();
    assert_app_info_state(0x01u);
}

static void test_an_application_whose_init_failed_is_failed_on_the_wire(void)
{
    tfw_mock_app_set_init(TFW_ERR_BAD_STATE);
    tfw_app_runner_start();
    assert_app_info_state(0x02u);
}

static void test_a_stopped_application_is_stopped_on_the_wire(void)
{
    tfw_app_runner_start();
    tfw_app_runner_stop(0u);
    assert_app_info_state(0x04u);
}

static void test_asking_app_info_calls_no_hook(void)
{
    tfw_app_runner_start();
    assert_app_info_state(0x01u);
    assert_app_info_state(0x01u);
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
}

/* ---- the configuration region's refusals ---- */

/* A refused read leaves the caller's buffer as it was. */
static void test_a_config_read_past_the_declared_length_is_refused_and_the_buffer_stays(void)
{
    uint8_t buf[2] = { 0x5Au, 0x5Au };

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_read(15u, buf, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, buf[1]);
}

/* The descriptor may declare more than the part holds; the read still stops
 * at the part's end. */
static void test_a_config_read_past_the_eeprom_is_refused_and_the_buffer_stays(void)
{
    uint8_t b = 0x5Au;

    tfw_mock_app_descriptor()->config_len = 0xFFFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_app_config_read((uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE), &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
}

static void test_a_config_read_the_eeprom_refuses_returns_the_eeprom_status(void)
{
    uint8_t b = 0x5Au;

    tfw_mock_eeprom_read_fails(1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_app_config_read(0u, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
}

static void test_a_config_write_past_the_declared_length_writes_no_byte(void)
{
    uint8_t  b = 0x11u;
    uint16_t lo;
    uint16_t hi;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_write(16u, &b, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

static void test_an_empty_config_write_past_the_end_is_refused(void)
{
    uint8_t  b = 0x11u;
    uint16_t lo;
    uint16_t hi;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_write(17u, &b, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

static void test_a_config_write_past_the_eeprom_writes_no_byte(void)
{
    uint8_t  b = 0x11u;
    uint16_t lo;
    uint16_t hi;

    tfw_mock_app_descriptor()->config_len = 0xFFFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_app_config_write((uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE), &b, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

/* off + len is summed in 32 bits: an offset at the top does not wrap to a
 * span inside the region. */
static void test_a_config_offset_at_the_top_does_not_wrap_into_the_region(void)
{
    uint8_t  buf[2] = { 0x11u, 0x22u };
    uint16_t lo;
    uint16_t hi;

    tfw_mock_app_descriptor()->config_len = 0xFFFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_write(0xFFFFu, buf, 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_read(0xFFFFu, buf, 2u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_HEX8(0x11u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, buf[1]);
}


int main(void)
{
    UNITY_BEGIN();
    /* First: the runner's state lives until the next reset, and a process is
     * one reset, so IDLE is only seen before any test has started it. */
    RUN_TEST(test_an_application_not_yet_started_is_idle_on_the_wire);
    RUN_TEST(test_a_running_application_is_running_on_the_wire);
    RUN_TEST(test_an_application_whose_init_failed_is_failed_on_the_wire);
    RUN_TEST(test_a_stopped_application_is_stopped_on_the_wire);
    RUN_TEST(test_asking_app_info_calls_no_hook);
    RUN_TEST(test_a_config_read_past_the_declared_length_is_refused_and_the_buffer_stays);
    RUN_TEST(test_a_config_read_past_the_eeprom_is_refused_and_the_buffer_stays);
    RUN_TEST(test_a_config_read_the_eeprom_refuses_returns_the_eeprom_status);
    RUN_TEST(test_a_config_write_past_the_declared_length_writes_no_byte);
    RUN_TEST(test_an_empty_config_write_past_the_end_is_refused);
    RUN_TEST(test_a_config_write_past_the_eeprom_writes_no_byte);
    RUN_TEST(test_a_config_offset_at_the_top_does_not_wrap_into_the_region);
    return UNITY_END();
}
