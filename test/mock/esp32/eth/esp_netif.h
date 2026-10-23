/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's esp_netif.h, for
 * test/unit/test_esp_eth_mutate.c alone: the types, the IP event and the
 * calls src/hal/esp32/hal_eth.c makes, with ESP-IDF's values. The test
 * implements the calls.
 */
#ifndef TFW_MOCK_ESP32_ETH_ESP_NETIF_H
#define TFW_MOCK_ESP32_ETH_ESP_NETIF_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"

#define ESP_ERR_ESP_NETIF_BASE                 0x5000
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED (ESP_ERR_ESP_NETIF_BASE + 0x05)

typedef struct {
    uint32_t addr;   /* network order: the first octet is the first byte */
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

#define esp_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t *)(&(ipaddr)->addr))[idx])
#define esp_ip4_addr1_16(ipaddr)           ((uint16_t)esp_ip4_addr_get_byte(ipaddr, 0))
#define esp_ip4_addr2_16(ipaddr)           ((uint16_t)esp_ip4_addr_get_byte(ipaddr, 1))
#define esp_ip4_addr3_16(ipaddr)           ((uint16_t)esp_ip4_addr_get_byte(ipaddr, 2))
#define esp_ip4_addr4_16(ipaddr)           ((uint16_t)esp_ip4_addr_get_byte(ipaddr, 3))

typedef struct esp_netif_obj esp_netif_t;
typedef void                *esp_netif_iodriver_handle;

/* Only which default it is: the test checks the Ethernet one was asked for. */
typedef struct {
    int kind;
} esp_netif_config_t;

#define ESP_NETIF_DEFAULT_ETH() { .kind = 0x45 }

extern const esp_event_base_t IP_EVENT;

typedef enum {
    IP_EVENT_STA_GOT_IP,
    IP_EVENT_STA_LOST_IP,
    IP_EVENT_AP_STAIPASSIGNED,
    IP_EVENT_GOT_IP6,
    IP_EVENT_ETH_GOT_IP,
    IP_EVENT_ETH_LOST_IP,
    IP_EVENT_PPP_GOT_IP,
    IP_EVENT_PPP_LOST_IP,
} ip_event_t;

esp_err_t    esp_netif_init(void);
esp_netif_t *esp_netif_new(const esp_netif_config_t *config);
esp_err_t    esp_netif_attach(esp_netif_t *netif, esp_netif_iodriver_handle driver);
esp_err_t    esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
esp_err_t    esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *info);
esp_err_t    esp_netif_str_to_ip4(const char *src, esp_ip4_addr_t *dst);
esp_err_t    esp_netif_dhcpc_stop(esp_netif_t *netif);

#endif
