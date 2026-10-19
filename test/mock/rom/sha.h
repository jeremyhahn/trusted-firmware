#ifndef TFW_MOCK_ROM_SHA_H
#define TFW_MOCK_ROM_SHA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ESP-IDF v5.5.5's components/esp_rom/esp32/include/esp32/rom/sha.h, the
 * classic ESP32's ROM SHA, declarations only, for the host build of
 * boot/hal/esp32/measure_esp32.c with CONFIG_IDF_TARGET_ESP32. The C3's and
 * the P4's ROM SHA has another signature, which layer 0 never calls. The
 * definition is test/mock/esp_sha_mock.c. */
typedef struct SHAContext {
    bool     start;
    uint32_t total_input_bits[4];
} SHA_CTX;

enum SHA_TYPE {
    SHA1 = 0,
    SHA2_256,
    SHA2_384,
    SHA2_512,

    SHA_INVALID = -1,
};

void ets_sha_init(SHA_CTX *ctx);
void ets_sha_enable(void);
void ets_sha_disable(void);
void ets_sha_update(SHA_CTX *ctx, enum SHA_TYPE type, const uint8_t *input, size_t input_bits);
void ets_sha_finish(SHA_CTX *ctx, enum SHA_TYPE type, uint8_t *output);

#endif
