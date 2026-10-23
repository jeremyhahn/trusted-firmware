/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_eth.h, for
 * test/unit/test_esp_eth_mutate.c alone: the MAC, PHY and driver configs
 * src/hal/esp32/hal_eth.c fills, with ESP-IDF's defaults, and the calls it
 * makes. The test implements the calls.
 */
#ifndef TFW_MOCK_ESP32_ETH_ESP_ETH_H
#define TFW_MOCK_ESP32_ETH_ESP_ETH_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct esp_eth_mac_s esp_eth_mac_t;
typedef struct esp_eth_phy_s esp_eth_phy_t;
typedef void                *esp_eth_handle_t;
typedef void                *esp_eth_netif_glue_handle_t;

typedef struct {
    uint32_t sw_reset_timeout_ms;
    uint32_t rx_task_stack_size;
    uint32_t flags;
} eth_mac_config_t;

#define ETH_MAC_DEFAULT_CONFIG() \
    { .sw_reset_timeout_ms = 100, .rx_task_stack_size = 4096, .flags = 0 }

typedef struct {
    int32_t  phy_addr;
    uint32_t reset_timeout_ms;
    uint32_t autonego_timeout_ms;
    int      reset_gpio_num;
} eth_phy_config_t;

#define ETH_PHY_DEFAULT_CONFIG()                                      \
    { .phy_addr = -1, .reset_timeout_ms = 100, .autonego_timeout_ms = 4000, \
      .reset_gpio_num = 5 }

typedef enum { EMAC_CLK_DEFAULT, EMAC_CLK_EXT_IN, EMAC_CLK_OUT } emac_rmii_clock_mode_t;
typedef int emac_rmii_clock_gpio_t;

typedef struct {
    struct {
        int mdc_num;
        int mdio_num;
    } smi_gpio;
    struct {
        struct {
            emac_rmii_clock_mode_t clock_mode;
            emac_rmii_clock_gpio_t clock_gpio;
        } rmii;
    } clock_config;
    struct {
        struct {
            int tx_en_num, txd0_num, txd1_num, crs_dv_num, rxd0_num, rxd1_num;
        } rmii;
    } emac_dataif_gpio;
} eth_esp32_emac_config_t;

#define ETH_ESP32_EMAC_DEFAULT_CONFIG()                                              \
    { .smi_gpio = { .mdc_num = 23, .mdio_num = 18 },                                 \
      .clock_config = { .rmii = { .clock_mode = EMAC_CLK_DEFAULT, .clock_gpio = 0 } }, \
      .emac_dataif_gpio = { .rmii = { -1, -1, -1, -1, -1, -1 } } }

typedef struct {
    esp_eth_mac_t *mac;
    esp_eth_phy_t *phy;
    uint32_t       check_link_period_ms;
} esp_eth_config_t;

#define ETH_DEFAULT_CONFIG(emac, ephy) \
    { .mac = (emac), .phy = (ephy), .check_link_period_ms = 2000 }

esp_eth_mac_t *esp_eth_mac_new_esp32(const eth_esp32_emac_config_t *esp32_config,
                                     const eth_mac_config_t        *config);
esp_eth_mac_t *esp_eth_mac_new_openeth(const eth_mac_config_t *config);
esp_eth_phy_t *esp_eth_phy_new_ip101(const eth_phy_config_t *config);
esp_eth_phy_t *esp_eth_phy_new_dp83848(const eth_phy_config_t *config);
esp_err_t      esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *out);
esp_err_t      esp_eth_start(esp_eth_handle_t hdl);
esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t hdl);

#endif
