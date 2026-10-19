#ifndef TFW_MOCK_ROM_ECDSA_H
#define TFW_MOCK_ROM_ECDSA_H

#include <stdint.h>

/* ESP-IDF v5.5.5's components/esp_rom/esp32p4/include/esp32p4/rom/ecdsa.h,
 * declarations only, for the host build of boot/hal/esp32/verify_p256_rom.c.
 * The definition is test/mock/esp_rom_ecdsa_mock.c. */
#define ETS_DIGEST_SHA256_LEN 32

typedef enum {
    ECDSA_CURVE_P192 = 1,
    ECDSA_CURVE_P256 = 2,
    ECDSA_CURVE_P384 = 3
} ECDSA_CURVE;

int ets_ecdsa_verify(const uint8_t *key, const uint8_t *sig, ECDSA_CURVE curve_id,
                     const uint8_t *digest, uint8_t *verified_digest);

#endif
