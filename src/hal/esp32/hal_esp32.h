/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What the ESP32 HAL's own files say to each other. Nothing outside
 * src/hal/esp32/ includes this.
 */
#ifndef TFW_HAL_ESP32_H
#define TFW_HAL_ESP32_H

#include <stdint.h>
#include "esp_system.h"
#include "tfw/status.h"
#include "tfw/types.h"

/* The wire protocol's TCP port: 'T','F' as a big-endian u16. IANA lists
 * 21555-21589 as unassigned. See docs/multi-arch/adr-esp-idf-integration.md. */
#define TFW_ESP_WIRE_PORT 21574u

/* How long the wire's host must have been quiet before a new connection may
 * take its place (hal_uart.c). Longer than any gap inside a session, where a
 * host sends its next request as soon as a reply lands; short enough that a
 * host which restarted is back on within a retry or two. */
#define TFW_ESP_WIRE_IDLE_MS 2000u

/* The board's own bring-up, from tfw_hal_init (hal_board.c). */
tfw_status_t tfw_esp_board_init(void);

/* Brings the Ethernet link and its netif up (hal_eth.c). Idempotent; the
 * address comes from DHCP, or from the board's static one. Returns TFW_ERR_INTERNAL if ESP-IDF refuses. */
tfw_status_t tfw_esp_eth_start(void);

/* esp_reset_reason() to the wire's reset cause (hal_sys.c). */
tfw_reset_cause_t tfw_esp_reset_decode(esp_reset_reason_t reason);

/* Reads `len` bytes at `off` from the named data partition, refusing a span
 * past its end rather than transferring part of it (hal_eeprom.c). */
tfw_status_t tfw_esp_part_read(const char *label, uint32_t off, uint8_t *buf, uint32_t len);

/* Writes `len` bytes at `off`, rewriting the 4 KB sectors it touches. */
tfw_status_t tfw_esp_part_write(const char *label, uint32_t off, const uint8_t *buf, uint32_t len);

/* The partitions this HAL owns, by label. Each board's partitions.csv names
 * them; a table without one makes the capability behind it refuse. */
#define TFW_ESP_PART_EEPROM   "tfw_eeprom"   /* the host-visible EEPROM capability */
#define TFW_ESP_PART_PLATFORM "tfw_plat"     /* the platform block (TFW_BOOT_EE_*) */
#define TFW_ESP_PART_IDENTITY "tfw_id"       /* reserved, not read: the DeviceID is derived (crypto_hmac_p256.c) */
#define TFW_ESP_PART_AUTH     "tfw_auth"     /* OTA_ACTIVATE's authorization (TFW_BOOT_AUTH_*) */

#endif /* TFW_HAL_ESP32_H */
