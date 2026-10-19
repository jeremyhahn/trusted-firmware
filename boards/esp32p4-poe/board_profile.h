/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ESP32-P4 on a PoE Ethernet board, with an ESP32-C6 co-processor.
 *
 * The board on the bench is Waveshare's ESP32-P4-WIFI6-POE-ETH, and every pin
 * below is checked against its schematic (docs/reference/README.md, "Board
 * sources"; the net and part per pin are on docs/development/bench/
 * esp32-p4.md, "Pins"). It has not yet booted this firmware: that is bench
 * HIL-0, the same page.
 *
 * Reserved, so absent from the pin table: the RMII lines and the IP101's
 * MDC/MDIO/reset (28-31, 34, 35, 49-52; 35 is also the BOOT key), the C6's
 * SDIO (14-19) and its enable (54), USB (24-27, on the header but the P4's
 * full-speed pins), the console UART0 (37, 38, to the CH343P bridge), the
 * strap 36, the ES8311 codec's I2S (9-13), the SD card (39-45) and the 32 kHz
 * crystal (0, 1).
 */
#ifndef TFW_BOARD_ESP32P4_POE_H
#define TFW_BOARD_ESP32P4_POE_H

#define TFW_BOARD_NAME "esp32p4-poe"
#define TFW_BOARD_ID   0x80u       /* the first id in the ESP32 range */

/* UUIDv5(TFW_SUIT_VENDOR_ID, "esp32p4-poe"): the SUIT class id a manifest for
 * this board names (issue #78). b7535d26-c463-598f-a55f-ecc936e674e1. */
#define TFW_SUIT_CLASS_ID { 0xb7u, 0x53u, 0x5du, 0x26u, 0xc4u, 0x63u, 0x59u, 0x8fu, \
                            0xa5u, 0x5fu, 0xecu, 0xc9u, 0x36u, 0xe6u, 0x74u, 0xe1u }

#define TFW_PIN_COUNT  17u
#define TFW_PORT_COUNT 2u          /* GPIO 0..31 and 32..54 */
#define TFW_PWM_COUNT  4u          /* LEDC channels 0..3 on timer 0 */
#define TFW_ADC_COUNT  8u          /* ADC1 channels 0..7, GPIO 16..23 */
#define TFW_EEPROM_SIZE 4096u      /* the tfw_eeprom partition */

/* ATTEST signs with the P-256 DeviceID the hmac-p256 tier derives from the
 * eFuse UDS (src/hal/esp32/crypto_hmac_p256.c). A tier with no UDS -- none,
 * on a part with no HMAC peripheral -- answers it NOT_PROVISIONED or
 * UNSUPPORTED, as an AVR board with no element does. */
#define TFW_FEATURE_ATTEST 1

/* Ethernet: the P4's EMAC and an IP101GRI PHY over RMII, the clock coming in
 * on GPIO50 from the PHY, which makes 50 MHz from its own 25 MHz crystal.
 * Address 1 is the PHY's strapping: AD0 pulled up, AD3 pulled down. */
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

/* Buses: I2C on the header's 7/8, shared on the board with the ES8311 codec
 * and the camera and display connectors; SPI on 46/47/48. */
#define TFW_ESP_I2C_SDA_GPIO  7
#define TFW_ESP_I2C_SCL_GPIO  8
#define TFW_ESP_SPI_MOSI_GPIO 46
#define TFW_ESP_SPI_MISO_GPIO 47
#define TFW_ESP_SPI_SCK_GPIO  48

/* The trial deadline (src/core/ota_trial.c): an image ESP-IDF boots as
 * PENDING_VERIFY that has not had OTA_CONFIRM by then resets, and its
 * bootloader falls back. 60 s, as each AVR board with a boot section. */
#define TFW_OTA_TRIAL_TIMEOUT_S 60u

/* The trailer parser's bound (TFW_BOOT_MAX_IMAGE_LEN), read by layer 0's gate
 * and by the app's OTA_ACTIVATE check (src/hal/esp32/hal_ota.c), which refuses
 * what layer 0 would refuse and so must share its bound. Offsets are
 * slot-relative and every read is bounded by the slot's size from the
 * partition table: 16 MiB is past any slot. config.h's default, a 32 KB AVR,
 * would refuse any ESP-IDF app with TFW_ERR_RANGE. */
#define TFW_FLASH_TOTAL      0x1000000uL
#define TFW_APP_REGION_BASE  0uL
#define TFW_APP_REGION_SIZE  0x1000000uL
#define TFW_BOOT_REGION_SIZE 0uL   /* ESP-IDF's bootloader, outside the slots */

#endif /* TFW_BOARD_ESP32P4_POE_H */
