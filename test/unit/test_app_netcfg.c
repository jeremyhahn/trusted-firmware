/*
 * The application runner on an image that links the Ethernet network block
 * (#640). The network block (include/tfw/eth.h, TFW_NETCFG_ADDR) and the
 * application's configuration (include/tfw/app.h, TFW_APP_CONFIG_BASE) both
 * start at TFW_BOOT_EE_LEN, so on such an image an application that owns
 * configuration is refused and its config calls never touch the network
 * block. An application that owns none still runs. Built with
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

static void test_an_application_owning_config_is_unsupported_on_an_ethernet_image(void)
{
    TEST_ASSERT_NOT_EQUAL(0, tfw_mock_app_descriptor()->config_len);
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
}

static void test_a_config_write_on_an_ethernet_image_is_refused_and_leaves_the_network_block(void)
{
    const uint8_t in[4] = { 0xA1u, 0xA2u, 0xA3u, 0xA4u };
    uint8_t       want[TFW_NETCFG_LEN];
    uint8_t       out[4];
    uint8_t       i;

    for (i = 0u; i < TFW_NETCFG_LEN; i++) {
        want[i] = (uint8_t)(0x5Au ^ i);
        tfw_mock_eeprom()[TFW_NETCFG_ADDR + i] = want[i];
    }
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_app_config_write(0u, in, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, &tfw_mock_eeprom()[TFW_NETCFG_ADDR], TFW_NETCFG_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_app_config_read(0u, out, 4u));
}

static void test_an_application_owning_no_config_runs_on_an_ethernet_image(void)
{
    tfw_mock_app_descriptor()->config_len = 0u;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_application_owning_config_is_unsupported_on_an_ethernet_image);
    RUN_TEST(test_a_config_write_on_an_ethernet_image_is_refused_and_leaves_the_network_block);
    RUN_TEST(test_an_application_owning_no_config_runs_on_an_ethernet_image);
    return UNITY_END();
}
