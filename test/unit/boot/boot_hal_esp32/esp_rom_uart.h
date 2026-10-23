/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for ESP-IDF's esp_rom_uart.h: the ROM console, polled. Both
 * return 0 on success, as the ROM does.
 */
#ifndef TFW_STANDIN_ESP_ROM_UART_H
#define TFW_STANDIN_ESP_ROM_UART_H

#include <stdint.h>

int esp_rom_output_rx_one_char(uint8_t *c);
int esp_rom_output_tx_one_char(uint8_t c);

#endif
