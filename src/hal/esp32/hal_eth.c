/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Ethernet link the wire protocol runs over, composed from ESP-IDF.
 *
 * Nothing here is ours but the choice of driver: esp_eth owns the MAC and the
 * PHY, esp_netif and lwIP own the IP stack and DHCP. The board says which
 * MAC through its sdkconfig.defaults, and the pins through board_profile.h:
 *
 *   CONFIG_ETH_USE_ESP32_EMAC  the part's own EMAC and an RMII PHY -- the
 *                              ESP32-P4's IP101 on the PoE board
 *   CONFIG_ETH_USE_OPENETH     the OpenCores MAC Espressif's QEMU emulates,
 *                              with the DP83848 PHY model it answers as
 *
 * The address comes from DHCP, unless the board sets CONFIG_TFW_ETH_STATIC_IP
 * (mk/arch/esp32/main/Kconfig.projbuild) and gives an address, mask and
 * gateway for a network with no DHCP server. Nothing in the wire protocol
 * depends on which.
 */

#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "sdkconfig.h"

#include "tfw/config.h"
#include "hal_esp32.h"

static const char *TAG = "tfw-eth";

static uint8_t started;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_netif_ip_info_t info;

    (void)base; (void)id; (void)data;
    /* The address from the netif (arg) rather than the event, and byte by byte
     * rather than through IPSTR: a format string spliced from a macro is opaque
     * to the host lint, which has no lwIP. */
    if (esp_netif_get_ip_info((esp_netif_t *)arg, &info) != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "link up, address %u.%u.%u.%u, wire on tcp/%u",
             (unsigned)esp_ip4_addr1_16(&info.ip), (unsigned)esp_ip4_addr2_16(&info.ip),
             (unsigned)esp_ip4_addr3_16(&info.ip), (unsigned)esp_ip4_addr4_16(&info.ip),
             (unsigned)TFW_ESP_WIRE_PORT);
}

/* The part's EMAC, or else QEMU's OpenCores MAC: mk/arch/esp32/main/CMakeLists.txt
 * refuses a board whose sdkconfig selects neither, so the #else is OpenETH. It
 * is also what the host lint reads, which has no sdkconfig. */
static esp_eth_mac_t *new_mac(const eth_mac_config_t *mac_config)
{
#if CONFIG_ETH_USE_ESP32_EMAC
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    emac.smi_gpio.mdc_num  = TFW_ESP_ETH_MDC_GPIO;
    emac.smi_gpio.mdio_num = TFW_ESP_ETH_MDIO_GPIO;
#ifdef TFW_ESP_ETH_RMII_CLK_GPIO
    emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac.clock_config.rmii.clock_gpio = (emac_rmii_clock_gpio_t)TFW_ESP_ETH_RMII_CLK_GPIO;
#endif
#ifdef TFW_ESP_ETH_RMII_TXD0_GPIO
    emac.emac_dataif_gpio.rmii.tx_en_num  = TFW_ESP_ETH_RMII_TX_EN_GPIO;
    emac.emac_dataif_gpio.rmii.txd0_num   = TFW_ESP_ETH_RMII_TXD0_GPIO;
    emac.emac_dataif_gpio.rmii.txd1_num   = TFW_ESP_ETH_RMII_TXD1_GPIO;
    emac.emac_dataif_gpio.rmii.crs_dv_num = TFW_ESP_ETH_RMII_CRS_DV_GPIO;
    emac.emac_dataif_gpio.rmii.rxd0_num   = TFW_ESP_ETH_RMII_RXD0_GPIO;
    emac.emac_dataif_gpio.rmii.rxd1_num   = TFW_ESP_ETH_RMII_RXD1_GPIO;
#endif
    return esp_eth_mac_new_esp32(&emac, mac_config);
#else
    return esp_eth_mac_new_openeth(mac_config);
#endif
}

static esp_eth_phy_t *new_phy(void)
{
    eth_phy_config_t phy = ETH_PHY_DEFAULT_CONFIG();

#if CONFIG_ETH_USE_ESP32_EMAC
    phy.phy_addr       = TFW_ESP_ETH_PHY_ADDR;
    phy.reset_gpio_num = TFW_ESP_ETH_PHY_RST_GPIO;
    return esp_eth_phy_new_ip101(&phy);
#else
    phy.autonego_timeout_ms = 100;
    return esp_eth_phy_new_dp83848(&phy);
#endif
}

#if CONFIG_TFW_ETH_STATIC_IP
/* The board's address, mask and gateway (Kconfig.projbuild) in place of DHCP.
 * The DHCP client is stopped first: esp_netif will not take an address while
 * it could still be asked for one. mk/arch/esp32/main/CMakeLists.txt has
 * already refused a value that is not a dotted quad. */
static esp_err_t use_static_address(esp_netif_t *netif)
{
    esp_netif_ip_info_t info;
    esp_err_t           err;

    if (esp_netif_str_to_ip4(CONFIG_TFW_ETH_IP_ADDR, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_TFW_ETH_NETMASK, &info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(CONFIG_TFW_ETH_GATEWAY, &info.gw) != ESP_OK) {
        return ESP_FAIL;
    }
    err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }
    return esp_netif_set_ip_info(netif, &info);
}
#endif

tfw_status_t tfw_esp_eth_start(void)
{
    esp_netif_config_t      netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_mac_config_t        mac_cfg   = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_handle_t        eth       = NULL;
    esp_netif_t            *netif;
    esp_eth_mac_t          *mac;
    esp_eth_phy_t          *phy;

    if (started) {
        return TFW_OK;
    }
    if (esp_netif_init() != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    /* ESP_ERR_INVALID_STATE: something else made the loop first. */
    {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return TFW_ERR_INTERNAL;
        }
    }
    netif = esp_netif_new(&netif_cfg);
    mac   = new_mac(&mac_cfg);
    phy   = new_phy();
    if (netif == NULL || mac == NULL || phy == NULL) {
        return TFW_ERR_INTERNAL;
    }
#if CONFIG_TFW_ETH_STATIC_IP
    if (use_static_address(netif) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
#endif
    {
        esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
        if (esp_eth_driver_install(&cfg, &eth) != ESP_OK) {
            return TFW_ERR_INTERNAL;
        }
    }
    if (esp_netif_attach(netif, esp_eth_new_netif_glue(eth)) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, netif) != ESP_OK ||
        esp_eth_start(eth) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    started = 1u;
    return TFW_OK;
}
