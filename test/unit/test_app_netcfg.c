/*
 * The application runner on an image that links the Ethernet network block
 * (#640, owner ruling 1234). The network block (include/tfw/eth.h,
 * [TFW_NETCFG_ADDR, TFW_NETCFG_ADDR + TFW_NETCFG_LEN) = [0x140, 0x160)) is
 * reserved on every image, and the application's configuration
 * (include/tfw/app.h, TFW_APP_CONFIG_BASE) starts at 0x160 above it. So on an
 * Ethernet image an application that owns configuration runs, its config
 * calls land at 0x160 + offset and never touch the network block, and only
 * configuration past the EEPROM end is refused. Built with
 * -DTFW_FEATURE_ETHERNET=1 (mk/host.mk).
 */
#include <string.h>
#include "unity.h"
#include "sys.h"
#include "app_runner.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "tfw/eth.h"

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
}
void tearDown(void) {}

static void seed_netcfg(uint8_t *want, uint8_t salt)
{
    uint8_t i;

    for (i = 0u; i < TFW_NETCFG_LEN; i++) {
        want[i] = (uint8_t)(salt ^ i);
        tfw_mock_eeprom()[TFW_NETCFG_ADDR + i] = want[i];
    }
}

static void test_an_application_owning_config_runs_on_an_ethernet_image(void)
{
    TEST_ASSERT_NOT_EQUAL(0, tfw_mock_app_descriptor()->config_len);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

static void test_config_writes_land_above_the_network_block_and_read_back(void)
{
    uint8_t  want[TFW_NETCFG_LEN];
    uint8_t  in[4];
    uint8_t  out[4];
    uint16_t len = tfw_mock_app_descriptor()->config_len;
    uint16_t off;
    uint8_t  n;
    uint8_t  i;

    TEST_ASSERT_NOT_EQUAL(0, len);
    seed_netcfg(want, 0x5Au);
    tfw_app_runner_start();
    for (off = 0u; off < len; off = (uint16_t)(off + n)) {
        n = (uint8_t)((uint16_t)(len - off) < 4u ? (uint16_t)(len - off) : 4u);
        for (i = 0u; i < n; i++) {
            in[i] = (uint8_t)(0xA0u + off + i);
        }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_write(off, in, n));
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &tfw_mock_eeprom()[TFW_NETCFG_ADDR], TFW_NETCFG_LEN);
    for (off = 0u; off < len; off++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u + off),
                               tfw_mock_eeprom()[TFW_APP_CONFIG_BASE + off]);
    }
    for (off = 0u; off < len; off = (uint16_t)(off + n)) {
        n = (uint8_t)((uint16_t)(len - off) < 4u ? (uint16_t)(len - off) : 4u);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_read(off, out, n));
        for (i = 0u; i < n; i++) {
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u + off + i), out[i]);
        }
    }
}

static void test_a_zero_length_config_call_on_an_ethernet_image_is_ok(void)
{
    const uint8_t in[1] = { 0xA1u };
    uint8_t       want[TFW_NETCFG_LEN];
    uint8_t       out[1] = { 0u };

    TEST_ASSERT_NOT_EQUAL(0, tfw_mock_app_descriptor()->config_len);
    seed_netcfg(want, 0x3Cu);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_read(0u, out, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_app_config_write(0u, in, 0u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &tfw_mock_eeprom()[TFW_NETCFG_ADDR], TFW_NETCFG_LEN);
}

static void test_an_out_of_range_config_call_on_an_ethernet_image_is_a_range_error(void)
{
    const uint8_t in[1] = { 0xA1u };
    uint8_t       out[1];
    uint16_t      len = tfw_mock_app_descriptor()->config_len;

    TEST_ASSERT_NOT_EQUAL(0, len);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_read(len, out, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_app_config_write(len, in, 1u));
}

static void test_config_past_the_eeprom_end_is_unsupported_on_an_ethernet_image(void)
{
    tfw_mock_app_descriptor()->config_len =
        (uint16_t)(TFW_EEPROM_SIZE - TFW_APP_CONFIG_BASE + 1u);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_application_owning_config_runs_on_an_ethernet_image);
    RUN_TEST(test_config_writes_land_above_the_network_block_and_read_back);
    RUN_TEST(test_a_zero_length_config_call_on_an_ethernet_image_is_ok);
    RUN_TEST(test_an_out_of_range_config_call_on_an_ethernet_image_is_a_range_error);
    RUN_TEST(test_config_past_the_eeprom_end_is_unsupported_on_an_ethernet_image);
    return UNITY_END();
}
