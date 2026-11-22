/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's bootloader_random.h, for the suites
 * that link src/hal/esp32/crypto_hmac_p256.c: the entropy source's switch
 * tfw_crypto_random throws around its draw (#607). Each suite implements it.
 */
#ifndef TFW_MOCK_ESP32_BOOTLOADER_RANDOM_H
#define TFW_MOCK_ESP32_BOOTLOADER_RANDOM_H

void bootloader_random_enable(void);
void bootloader_random_disable(void);

#endif
