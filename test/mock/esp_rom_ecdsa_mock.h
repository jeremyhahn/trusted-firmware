#ifndef TFW_ESP_ROM_ECDSA_MOCK_H
#define TFW_ESP_ROM_ECDSA_MOCK_H

#include <stdint.h>

/* The ROM's ets_ecdsa_verify on a host (esp_rom_ecdsa_mock.c). */
void tfw_rom_ecdsa_mock_reset(void);

/* Answer `ret` to every call instead of verifying, as a ROM that misread the
 * signature might; the reset verifies again. */
void tfw_rom_ecdsa_mock_answer(int ret);

/* Calls since the reset, and the curve and digest of the last one. */
unsigned       tfw_rom_ecdsa_mock_calls(void);
int            tfw_rom_ecdsa_mock_curve(void);
const uint8_t *tfw_rom_ecdsa_mock_digest(void);

#endif
