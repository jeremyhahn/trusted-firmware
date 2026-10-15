/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Espressif's QEMU, on its esp32c3 and esp32 machines.
 *
 * A board is a build target, not a part (docs/boards/board-id-registry.md),
 * and this one is a test target: the emulated machine Espressif ships, with
 * the OpenCores Ethernet MAC it models. The ESP32-P4 port is exercised here
 * until the PoE board arrives. One board id for both machines -- ids are wire
 * values and the fewest are taken -- because CAPS already tells a host the
 * one fact that differs between them, the crypto tier.
 *
 * The pins are the GPIOs both machines have and neither spends on its flash
 * or its console: 2, 4, 5, 18 and 19. QEMU models GPIO levels and little
 * else, so this table is here to keep the board honest, not to drive loads.
 * Neither machine models an ADC that this port uses, so no pin offers one.
 */
#ifndef TFW_BOARD_ESP32_QEMU_H
#define TFW_BOARD_ESP32_QEMU_H

#define TFW_BOARD_NAME "esp32-qemu"
#define TFW_BOARD_ID   0x81u

#define TFW_PIN_COUNT  5u
#define TFW_PORT_COUNT 1u          /* GPIO 0..31 */
#define TFW_PWM_COUNT  4u          /* LEDC channels 0..3 on timer 0 */
#define TFW_ADC_COUNT  0u
#define TFW_EEPROM_SIZE 4096u      /* the tfw_eeprom partition */

/* The wire has no identity to sign with yet on this port: see
 * src/hal/esp32/crypto_hmac_ds.c. */
#define TFW_FEATURE_ATTEST 0

/* Tone can drive any GPIO through LEDC; core allows one pin, and it is the
 * one with no PWM channel, so a tone never competes with a duty cycle. */
#define TFW_TONE_PIN   4u          /* GPIO19 */

/* The buses' pins. Neither machine models the peripherals, and the HAL brings
 * a bus up only on its first transfer, so these are never driven in QEMU. */
#define TFW_ESP_I2C_SDA_GPIO  4
#define TFW_ESP_I2C_SCL_GPIO  5
#define TFW_ESP_SPI_MOSI_GPIO 18
#define TFW_ESP_SPI_MISO_GPIO 19
#define TFW_ESP_SPI_SCK_GPIO  2

/* Board sizing deviations from config.h: none. The superloop's buffers are
 * the wire's, and the part has RAM to spare. */

#endif /* TFW_BOARD_ESP32_QEMU_H */
