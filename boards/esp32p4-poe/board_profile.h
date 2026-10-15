/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ESP32-P4 on a PoE Ethernet board, with an ESP32-C6 co-processor.
 *
 * NOT YET BROUGHT UP ON HARDWARE. The board has not arrived; every pin below
 * is the ESP32-P4 reference design's -- Espressif's ESP32-P4-Function-EV-Board,
 * whose Ethernet and C6 wiring the PoE boards built on it follow -- and is to
 * be confirmed against the board's schematic at bring-up before anything is
 * driven. ESP-IDF's own ETH_ESP32_EMAC_DEFAULT_CONFIG for the P4 names the
 * same RMII pins.
 *
 * Reserved, so absent from the pin table: the RMII lines and the PHY's
 * MDC/MDIO/reset (28-31, 34, 35, 49-52), the C6's SDIO (14-19) and its enable
 * (54), USB (24-27), the console UART0 (37, 38) and the remaining strapping
 * pins (34-36).
 */
#ifndef TFW_BOARD_ESP32P4_POE_H
#define TFW_BOARD_ESP32P4_POE_H

#define TFW_BOARD_NAME "esp32p4-poe"
#define TFW_BOARD_ID   0x80u       /* the first id in the ESP32 range */

#define TFW_PIN_COUNT  17u
#define TFW_PORT_COUNT 2u          /* GPIO 0..31 and 32..54 */
#define TFW_PWM_COUNT  4u          /* LEDC channels 0..3 on timer 0 */
#define TFW_ADC_COUNT  8u          /* ADC1 channels 0..7, GPIO 16..23 */
#define TFW_EEPROM_SIZE 4096u      /* the tfw_eeprom partition */

/* The wire has no identity to sign with yet on this port: see
 * src/hal/esp32/crypto_hmac_ds.c. */
#define TFW_FEATURE_ATTEST 0

/* Ethernet: the P4's EMAC and an IP101 PHY over RMII, the clock coming in
 * from the PHY's 50 MHz on GPIO50. */
#define TFW_ESP_ETH_PHY_ADDR          1
#define TFW_ESP_ETH_PHY_RST_GPIO      51
#define TFW_ESP_ETH_MDC_GPIO          31
#define TFW_ESP_ETH_MDIO_GPIO         52
#define TFW_ESP_ETH_RMII_CLK_GPIO     50
#define TFW_ESP_ETH_RMII_TX_EN_GPIO   49
#define TFW_ESP_ETH_RMII_TXD0_GPIO    34
#define TFW_ESP_ETH_RMII_TXD1_GPIO    35
#define TFW_ESP_ETH_RMII_CRS_DV_GPIO  28
#define TFW_ESP_ETH_RMII_RXD0_GPIO    29
#define TFW_ESP_ETH_RMII_RXD1_GPIO    30

/* The C6's enable line. Held low from the first instruction of tfw_hal_init,
 * so the C6 never runs and its radio is never enabled (src/hal/esp32/
 * hal_board.c). */
#define TFW_ESP_COPROC_EN_GPIO        54

/* Tone can drive any GPIO through LEDC; core allows one pin, and it is the
 * first with no PWM channel, so a tone never competes with a duty cycle. */
#define TFW_TONE_PIN   4u          /* GPIO6 */

/* Buses: I2C on the header's 7/8, SPI on 46/47/48. */
#define TFW_ESP_I2C_SDA_GPIO  7
#define TFW_ESP_I2C_SCL_GPIO  8
#define TFW_ESP_SPI_MOSI_GPIO 46
#define TFW_ESP_SPI_MISO_GPIO 47
#define TFW_ESP_SPI_SCK_GPIO  48

#endif /* TFW_BOARD_ESP32P4_POE_H */
