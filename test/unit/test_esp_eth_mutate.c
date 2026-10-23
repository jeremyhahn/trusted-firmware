/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 Ethernet link, src/hal/esp32/hal_eth.c.
 *
 * The unit had no host build. This builds it over stand-ins for the ESP-IDF
 * calls it makes (test/mock/esp32/eth/), each implemented here, counted and
 * able to fail. It is built once per arm the board picks:
 *
 *   test_esp_eth_mutate     QEMU's OpenCores MAC and DP83848 PHY, DHCP
 *   test_esp_eth_mutate_p4  the part's EMAC over RMII and the IP101 PHY on
 *                           the pins the board names, a static address
 *
 * and holds, in each: every failing call ending the start with
 * TFW_ERR_INTERNAL before the next is made, the default loop another made
 * first not counting as one, the MAC, PHY and driver configured as the arm
 * says, the event registered before the link starts, one start and no
 * second, and the address logged byte by byte when the link comes up.
 *
 * The start latches, so every test that fails it runs before the one that
 * does not.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "tfw/hal.h"
#include "hal_esp32.h"

struct esp_netif_obj { int x; };
struct esp_eth_mac_s { int x; };
struct esp_eth_phy_s { int x; };

const esp_event_base_t IP_EVENT = "IP_EVENT";

static struct esp_netif_obj netif_obj;
static struct esp_eth_mac_s mac_obj;
static struct esp_eth_phy_s phy_obj;
static int                  eth_obj, glue_obj;

/* What each call is made to do. */
static esp_err_t init_err, loop_err, dhcpc_err, set_ip_err, install_err;
static esp_err_t attach_err, register_err, start_err, get_ip_err;
static int       netif_null, mac_null, phy_null;
static unsigned  str_fail_at;   /* 1-based; 0 never */

/* What each call was given, and when. */
static unsigned seq;
static unsigned init_at, loop_at, new_at, mac_at, phy_at, str_at[3], str_n;
static unsigned dhcpc_at, set_ip_at, install_at, glue_at, attach_at, register_at, start_at;
static unsigned get_ip_n, logs;
static int                     netif_kind;
static eth_mac_config_t        mac_cfg;
static const eth_mac_config_t *mac_cfg_ptr;
static eth_esp32_emac_config_t emac;
static unsigned                emac_n, openeth_n, ip101_n, dp83848_n;
static eth_phy_config_t        phy_cfg;
static const char             *str_src[3];
static esp_ip4_addr_t         *str_dst[3];
static esp_netif_t            *dhcpc_netif, *set_ip_netif, *attach_netif, *get_ip_netif;
static esp_netif_ip_info_t     set_ip_info;
static esp_eth_config_t        install_cfg;
static esp_eth_handle_t        glue_eth, start_eth;
static esp_netif_iodriver_handle attach_driver;
static esp_event_base_t        reg_base;
static int32_t                 reg_id;
static esp_event_handler_t     reg_handler;
static void                   *reg_arg;
static esp_netif_ip_info_t     up_info;
static char                    log_level, log_tag[16], log_line[96];

esp_err_t esp_netif_init(void)
{
    init_at = ++seq;
    return init_err;
}

esp_err_t esp_event_loop_create_default(void)
{
    loop_at = ++seq;
    return loop_err;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *config)
{
    new_at     = ++seq;
    netif_kind = config->kind;
    return netif_null ? NULL : &netif_obj;
}

esp_eth_mac_t *esp_eth_mac_new_esp32(const eth_esp32_emac_config_t *esp32_config,
                                     const eth_mac_config_t        *config)
{
    mac_at      = ++seq;
    emac        = *esp32_config;
    mac_cfg     = *config;
    mac_cfg_ptr = config;
    emac_n++;
    return mac_null ? NULL : &mac_obj;
}

esp_eth_mac_t *esp_eth_mac_new_openeth(const eth_mac_config_t *config)
{
    mac_at      = ++seq;
    mac_cfg     = *config;
    mac_cfg_ptr = config;
    openeth_n++;
    return mac_null ? NULL : &mac_obj;
}

esp_eth_phy_t *esp_eth_phy_new_ip101(const eth_phy_config_t *config)
{
    phy_at  = ++seq;
    phy_cfg = *config;
    ip101_n++;
    return phy_null ? NULL : &phy_obj;
}

esp_eth_phy_t *esp_eth_phy_new_dp83848(const eth_phy_config_t *config)
{
    phy_at  = ++seq;
    phy_cfg = *config;
    dp83848_n++;
    return phy_null ? NULL : &phy_obj;
}

esp_err_t esp_netif_str_to_ip4(const char *src, esp_ip4_addr_t *dst)
{
    unsigned a, b, c, d;
    uint8_t *o = (uint8_t *)&dst->addr;

    TEST_ASSERT_LESS_THAN_UINT(3u, str_n);
    str_at[str_n]  = ++seq;
    str_src[str_n] = src;
    str_dst[str_n] = dst;
    str_n++;
    if (str_n == str_fail_at) {
        return ESP_FAIL;
    }
    TEST_ASSERT_EQUAL_INT(4, sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d));
    o[0] = (uint8_t)a; o[1] = (uint8_t)b; o[2] = (uint8_t)c; o[3] = (uint8_t)d;
    return ESP_OK;
}

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *netif)
{
    dhcpc_at    = ++seq;
    dhcpc_netif = netif;
    return dhcpc_err;
}

esp_err_t esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *info)
{
    set_ip_at    = ++seq;
    set_ip_netif = netif;
    set_ip_info  = *info;
    return set_ip_err;
}

esp_err_t esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *out)
{
    install_at  = ++seq;
    install_cfg = *config;
    *out        = &eth_obj;
    return install_err;
}

esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t hdl)
{
    glue_at  = ++seq;
    glue_eth = hdl;
    return &glue_obj;
}

esp_err_t esp_netif_attach(esp_netif_t *netif, esp_netif_iodriver_handle driver)
{
    attach_at     = ++seq;
    attach_netif  = netif;
    attach_driver = driver;
    return attach_err;
}

esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg)
{
    register_at = ++seq;
    reg_base    = base;
    reg_id      = id;
    reg_handler = handler;
    reg_arg     = arg;
    return register_err;
}

esp_err_t esp_eth_start(esp_eth_handle_t hdl)
{
    start_at  = ++seq;
    start_eth = hdl;
    return start_err;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    get_ip_n++;
    get_ip_netif = netif;
    *info        = up_info;
    return get_ip_err;
}

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;

    logs++;
    log_level = level;
    (void)snprintf(log_tag, sizeof log_tag, "%s", tag);
    va_start(ap, fmt);
    (void)vsnprintf(log_line, sizeof log_line, fmt, ap);
    va_end(ap);
}

void setUp(void)
{
    init_err = loop_err = dhcpc_err = set_ip_err = install_err = ESP_OK;
    attach_err = register_err = start_err = get_ip_err = ESP_OK;
    netif_null = mac_null = phy_null = 0;
    str_fail_at = 0u;
    seq = init_at = loop_at = new_at = mac_at = phy_at = str_n = 0u;
    memset(str_at, 0, sizeof str_at);
    dhcpc_at = set_ip_at = install_at = glue_at = attach_at = register_at = start_at = 0u;
    get_ip_n = logs = 0u;
    emac_n = openeth_n = ip101_n = dp83848_n = 0u;
    netif_kind  = 0;
    mac_cfg_ptr = NULL;
    memset(&mac_cfg, 0, sizeof mac_cfg);
    memset(&emac, 0, sizeof emac);
    memset(&phy_cfg, 0, sizeof phy_cfg);
    memset(&set_ip_info, 0, sizeof set_ip_info);
    memset(&install_cfg, 0, sizeof install_cfg);
    dhcpc_netif = set_ip_netif = attach_netif = get_ip_netif = NULL;
    glue_eth = start_eth = attach_driver = NULL;
    reg_base = NULL;
    reg_id   = -1;
    reg_handler = NULL;
    reg_arg  = NULL;
    log_level = 0;
    log_tag[0] = log_line[0] = '\0';
}

void tearDown(void) {}

static void assert_static_untouched(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, str_n);
    TEST_ASSERT_EQUAL_UINT(0u, dhcpc_at);
    TEST_ASSERT_EQUAL_UINT(0u, set_ip_at);
}

static void test_a_failed_netif_init_stops_first(void)
{
    init_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(1u, init_at);
    TEST_ASSERT_EQUAL_UINT(1u, seq);
}

static void test_a_failed_event_loop_stops(void)
{
    static const esp_err_t errs[] = { ESP_FAIL, 0x101 /* ESP_ERR_NO_MEM */, ESP_ERR_INVALID_ARG };
    unsigned i;

    for (i = 0u; i < sizeof errs / sizeof errs[0]; i++) {
        setUp();
        loop_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
        TEST_ASSERT_EQUAL_UINT(2u, loop_at);
        TEST_ASSERT_EQUAL_UINT(2u, seq);
    }
}

static void test_a_loop_another_made_first_is_not_a_failure(void)
{
    loop_err  = ESP_ERR_INVALID_STATE;
    start_err = ESP_FAIL;   /* runs to the end and still leaves it unstarted */
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_NOT_EQUAL(0u, start_at);
}

static void test_no_netif_mac_or_phy_stops_before_the_driver(void)
{
    unsigned which;

    for (which = 0u; which < 3u; which++) {
        setUp();
        netif_null = which == 0u;
        mac_null   = which == 1u;
        phy_null   = which == 2u;
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
        TEST_ASSERT_NOT_EQUAL(0u, phy_at);
        TEST_ASSERT_EQUAL_UINT(0u, install_at);
        assert_static_untouched();
    }
}

static void test_an_address_that_will_not_parse_stops(void)
{
    unsigned at;

    for (at = 1u; at <= 3u; at++) {
        setUp();
        str_fail_at = at;
        start_err   = ESP_FAIL;
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
        if (!CONFIG_TFW_ETH_STATIC_IP) {
            assert_static_untouched();
            continue;
        }
        TEST_ASSERT_EQUAL_UINT(at, str_n);
        TEST_ASSERT_EQUAL_UINT(0u, dhcpc_at);
        TEST_ASSERT_EQUAL_UINT(0u, install_at);
    }
}

static void test_a_dhcp_client_that_will_not_stop_stops(void)
{
    dhcpc_err = ESP_FAIL;
    start_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    if (!CONFIG_TFW_ETH_STATIC_IP) {
        assert_static_untouched();
        return;
    }
    TEST_ASSERT_NOT_EQUAL(0u, dhcpc_at);
    TEST_ASSERT_EQUAL_UINT(0u, set_ip_at);
    TEST_ASSERT_EQUAL_UINT(0u, install_at);
}

static void test_a_dhcp_client_already_stopped_is_not_a_failure(void)
{
    dhcpc_err = ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;
    start_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_NOT_EQUAL(0u, start_at);
    if (!CONFIG_TFW_ETH_STATIC_IP) {
        assert_static_untouched();
        return;
    }
    TEST_ASSERT_NOT_EQUAL(0u, set_ip_at);
}

static void test_an_address_not_taken_stops(void)
{
    set_ip_err = ESP_FAIL;
    start_err  = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    if (!CONFIG_TFW_ETH_STATIC_IP) {
        assert_static_untouched();
        return;
    }
    TEST_ASSERT_NOT_EQUAL(0u, set_ip_at);
    TEST_ASSERT_EQUAL_UINT(0u, install_at);
}

static void test_a_failed_driver_install_stops(void)
{
    install_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(seq, install_at);
}

static void test_a_failed_attach_register_or_start_stops(void)
{
    attach_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(seq, attach_at);

    setUp();
    register_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(seq, register_at);

    setUp();
    start_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(seq, start_at);
}

static void test_the_link_starts_as_the_board_says(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_eth_start());

    /* In order, each once. */
    TEST_ASSERT_EQUAL_UINT(1u, init_at);
    TEST_ASSERT_EQUAL_UINT(2u, loop_at);
    TEST_ASSERT_EQUAL_UINT(3u, new_at);
    TEST_ASSERT_EQUAL_UINT(4u, mac_at);
    TEST_ASSERT_EQUAL_UINT(5u, phy_at);
    TEST_ASSERT_LESS_THAN_UINT(install_at, phy_at);
    TEST_ASSERT_LESS_THAN_UINT(attach_at, glue_at);
    TEST_ASSERT_LESS_THAN_UINT(register_at, attach_at);
    TEST_ASSERT_LESS_THAN_UINT(start_at, register_at);
    TEST_ASSERT_EQUAL_UINT(seq, start_at);

    TEST_ASSERT_EQUAL_INT(0x45, netif_kind);
    TEST_ASSERT_NOT_NULL(mac_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT32(100u, mac_cfg.sw_reset_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(4096u, mac_cfg.rx_task_stack_size);
    TEST_ASSERT_EQUAL_UINT32(4000u - (CONFIG_ETH_USE_ESP32_EMAC ? 0u : 3900u),
                             phy_cfg.autonego_timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(100u, phy_cfg.reset_timeout_ms);
    if (CONFIG_ETH_USE_ESP32_EMAC) {
        TEST_ASSERT_EQUAL_UINT(1u, emac_n);
        TEST_ASSERT_EQUAL_UINT(0u, openeth_n);
        TEST_ASSERT_EQUAL_UINT(1u, ip101_n);
        TEST_ASSERT_EQUAL_UINT(0u, dp83848_n);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_MDC_GPIO, emac.smi_gpio.mdc_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_MDIO_GPIO, emac.smi_gpio.mdio_num);
        TEST_ASSERT_EQUAL_INT(EMAC_CLK_EXT_IN, emac.clock_config.rmii.clock_mode);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_CLK_GPIO, emac.clock_config.rmii.clock_gpio);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_TX_EN_GPIO, emac.emac_dataif_gpio.rmii.tx_en_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_TXD0_GPIO, emac.emac_dataif_gpio.rmii.txd0_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_TXD1_GPIO, emac.emac_dataif_gpio.rmii.txd1_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_CRS_DV_GPIO, emac.emac_dataif_gpio.rmii.crs_dv_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_RXD0_GPIO, emac.emac_dataif_gpio.rmii.rxd0_num);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_RMII_RXD1_GPIO, emac.emac_dataif_gpio.rmii.rxd1_num);
        TEST_ASSERT_EQUAL_INT32(TFW_ESP_ETH_PHY_ADDR, phy_cfg.phy_addr);
        TEST_ASSERT_EQUAL_INT(TFW_ESP_ETH_PHY_RST_GPIO, phy_cfg.reset_gpio_num);
    } else {
        TEST_ASSERT_EQUAL_UINT(0u, emac_n);
        TEST_ASSERT_EQUAL_UINT(1u, openeth_n);
        TEST_ASSERT_EQUAL_UINT(0u, ip101_n);
        TEST_ASSERT_EQUAL_UINT(1u, dp83848_n);
        TEST_ASSERT_EQUAL_INT32(-1, phy_cfg.phy_addr);
        TEST_ASSERT_EQUAL_INT(5, phy_cfg.reset_gpio_num);
    }

    if (CONFIG_TFW_ETH_STATIC_IP) {
        TEST_ASSERT_EQUAL_UINT(3u, str_n);
        TEST_ASSERT_EQUAL_STRING(CONFIG_TFW_ETH_IP_ADDR, str_src[0]);
        TEST_ASSERT_EQUAL_STRING(CONFIG_TFW_ETH_NETMASK, str_src[1]);
        TEST_ASSERT_EQUAL_STRING(CONFIG_TFW_ETH_GATEWAY, str_src[2]);
        TEST_ASSERT_LESS_THAN_UINT(dhcpc_at, str_at[2]);
        TEST_ASSERT_LESS_THAN_UINT(set_ip_at, dhcpc_at);
        TEST_ASSERT_LESS_THAN_UINT(install_at, set_ip_at);
        TEST_ASSERT_EQUAL_PTR(&netif_obj, dhcpc_netif);
        TEST_ASSERT_EQUAL_PTR(&netif_obj, set_ip_netif);
        TEST_ASSERT_EQUAL_HEX8_ARRAY("\xc0\x00\x02\x0a", &set_ip_info.ip.addr, 4);
        TEST_ASSERT_EQUAL_HEX8_ARRAY("\xff\xff\xff\x00", &set_ip_info.netmask.addr, 4);
        TEST_ASSERT_EQUAL_HEX8_ARRAY("\xc0\x00\x02\x01", &set_ip_info.gw.addr, 4);
    } else {
        assert_static_untouched();
    }

    TEST_ASSERT_EQUAL_PTR(&mac_obj, install_cfg.mac);
    TEST_ASSERT_EQUAL_PTR(&phy_obj, install_cfg.phy);
    TEST_ASSERT_EQUAL_UINT32(2000u, install_cfg.check_link_period_ms);
    TEST_ASSERT_EQUAL_PTR(&eth_obj, glue_eth);
    TEST_ASSERT_EQUAL_PTR(&netif_obj, attach_netif);
    TEST_ASSERT_EQUAL_PTR(&glue_obj, attach_driver);
    TEST_ASSERT_EQUAL_PTR(IP_EVENT, reg_base);
    TEST_ASSERT_EQUAL_INT32(IP_EVENT_ETH_GOT_IP, reg_id);
    TEST_ASSERT_NOT_NULL(reg_handler);
    TEST_ASSERT_EQUAL_PTR(&netif_obj, reg_arg);
    TEST_ASSERT_EQUAL_PTR(&eth_obj, start_eth);
    TEST_ASSERT_EQUAL_UINT(0u, logs);
}

static void test_a_second_start_does_nothing(void)
{
    init_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_eth_start());
    TEST_ASSERT_EQUAL_UINT(0u, seq);
}

/* The handler the start registered, kept past setUp. */
static esp_event_handler_t got_ip;

static void test_link_up_logs_the_netifs_address(void)
{
    uint8_t *o = (uint8_t *)&up_info.ip.addr;

    TEST_ASSERT_NOT_NULL(got_ip);
    o[0] = 198u; o[1] = 51u; o[2] = 100u; o[3] = 7u;
    got_ip(&netif_obj, IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    TEST_ASSERT_EQUAL_UINT(1u, get_ip_n);
    TEST_ASSERT_EQUAL_PTR(&netif_obj, get_ip_netif);
    TEST_ASSERT_EQUAL_UINT(1u, logs);
    TEST_ASSERT_EQUAL_CHAR('I', log_level);
    TEST_ASSERT_EQUAL_STRING("tfw-eth", log_tag);
    TEST_ASSERT_EQUAL_STRING("link up, address 198.51.100.7, wire on tcp/21574", log_line);

    get_ip_err = ESP_FAIL;
    got_ip(&netif_obj, IP_EVENT, IP_EVENT_ETH_GOT_IP, NULL);
    TEST_ASSERT_EQUAL_UINT(2u, get_ip_n);
    TEST_ASSERT_EQUAL_UINT(1u, logs);
}

static void test_the_start_then_the_handler(void)
{
    test_the_link_starts_as_the_board_says();
    got_ip = reg_handler;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_netif_init_stops_first);
    RUN_TEST(test_a_failed_event_loop_stops);
    RUN_TEST(test_a_loop_another_made_first_is_not_a_failure);
    RUN_TEST(test_no_netif_mac_or_phy_stops_before_the_driver);
    RUN_TEST(test_an_address_that_will_not_parse_stops);
    RUN_TEST(test_a_dhcp_client_that_will_not_stop_stops);
    RUN_TEST(test_a_dhcp_client_already_stopped_is_not_a_failure);
    RUN_TEST(test_an_address_not_taken_stops);
    RUN_TEST(test_a_failed_driver_install_stops);
    RUN_TEST(test_a_failed_attach_register_or_start_stops);
    RUN_TEST(test_the_start_then_the_handler);
    RUN_TEST(test_a_second_start_does_nothing);
    RUN_TEST(test_link_up_logs_the_netifs_address);
    return UNITY_END();
}
