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

/* UUIDv5(TFW_SUIT_VENDOR_ID, "esp32-qemu"): the SUIT class id a manifest for
 * this board names (issue #78). 1023e2f4-fdd0-5f12-9902-c62181fa5681. */
#define TFW_SUIT_CLASS_ID { 0x10u, 0x23u, 0xe2u, 0xf4u, 0xfdu, 0xd0u, 0x5fu, 0x12u, \
                            0x99u, 0x02u, 0xc6u, 0x21u, 0x81u, 0xfau, 0x56u, 0x81u }
/* The policy-set and script class ids, UUIDv5(TFW_SUIT_VENDOR_ID,
 * "esp32-qemu/policy") and (..., "esp32-qemu/script")
 * (docs/hsm-self-defence/adr-response-policy-format.md D2).
 * d14d34cc-9f71-50f4-8f9b-8b0ad4b37715, 45074df9-7e44-5aa0-ace4-2222abc5f7c3. */
#define TFW_SUIT_POLICY_CLASS_ID { 0xd1u, 0x4du, 0x34u, 0xccu, 0x9fu, 0x71u, 0x50u, 0xf4u, \
                                   0x8fu, 0x9bu, 0x8bu, 0x0au, 0xd4u, 0xb3u, 0x77u, 0x15u }
#define TFW_SUIT_SCRIPT_CLASS_ID { 0x45u, 0x07u, 0x4du, 0xf9u, 0x7eu, 0x44u, 0x5au, 0xa0u, \
                                   0xacu, 0xe4u, 0x22u, 0x22u, 0xabu, 0xc5u, 0xf7u, 0xc3u }

#define TFW_PIN_COUNT  5u
#define TFW_PORT_COUNT 1u          /* GPIO 0..31 */
#define TFW_PWM_COUNT  4u          /* LEDC channels 0..3 on timer 0 */
#define TFW_ADC_COUNT  0u
#define TFW_EEPROM_SIZE 4096u      /* the tfw_eeprom partition */

/* ATTEST signs with the P-256 DeviceID the hmac-p256 tier derives from the
 * eFuse UDS (src/hal/esp32/crypto_hmac_p256.c). A tier with no UDS -- none,
 * on a part with no HMAC peripheral -- answers it NOT_PROVISIONED or
 * UNSUPPORTED, as an AVR board with no element does. */
#define TFW_FEATURE_ATTEST 1

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

/* Board sizing deviations from config.h: the geometry only. The superloop's
 * buffers are the wire's, and the part has RAM to spare.
 *
 * The geometry is the trailer parser's bound (TFW_BOOT_MAX_IMAGE_LEN), read by
 * layer 0's gate and by the app's OTA_ACTIVATE check (src/hal/esp32/hal_ota.c),
 * which refuses what layer 0 would refuse and so must share its bound. Offsets
 * are slot-relative and every read is bounded by the slot's size from the
 * partition table: 16 MiB is past any slot. config.h's default, a 32 KB AVR,
 * would refuse any ESP-IDF app with TFW_ERR_RANGE. */
#define TFW_FLASH_TOTAL      0x1000000uL
#define TFW_APP_REGION_BASE  0uL
#define TFW_APP_REGION_SIZE  0x1000000uL
#define TFW_BOOT_REGION_SIZE 0uL   /* ESP-IDF's bootloader, outside the slots */

/* The trial deadline (src/core/ota_trial.c): an image ESP-IDF boots as
 * PENDING_VERIFY that has not had OTA_CONFIRM by then resets, and its
 * bootloader falls back. 60 s, as each AVR board with a boot section. */
#define TFW_OTA_TRIAL_TIMEOUT_S 60u

#endif /* TFW_BOARD_ESP32_QEMU_H */
