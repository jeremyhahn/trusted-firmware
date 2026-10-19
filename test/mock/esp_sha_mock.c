#include <stdint.h>
#include <string.h>
#include "bootloader_sha.h"
#include "rom/sha.h"
#include "sha256.h"
#include "esp_sha_mock.h"

/* ESP-IDF v5.5.5 components/bootloader_support/src/bootloader_sha.c:29-33 is
 * the C3's and the P4's bootloader_sha256_data, the ROM's ets_sha_update with
 * any length; :92-132 is the classic ESP32's, which asserts a multiple of 4
 * and reads uint32_t. The classic ROM's ets_sha_update keeps no partial
 * block in its context (esp32/rom/sha.h:25-28), so only its last call may
 * end inside one. One engine, as on the chip: one hash at a time. */

static tfw_sha256_t engine;
static int          words_only;
static int          enabled;
static int          rom_partial;
static unsigned     faults;
static unsigned     rom_updates;
static unsigned     boot_digests;

void tfw_esp_sha_mock_reset(int words)
{
    memset(&engine, 0, sizeof engine);
    words_only   = words;
    enabled      = 0;
    rom_partial  = 0;
    faults       = 0u;
    rom_updates  = 0u;
    boot_digests = 0u;
}

unsigned tfw_esp_sha_mock_faults(void) { return faults; }
unsigned tfw_esp_sha_mock_rom_updates(void) { return rom_updates; }
unsigned tfw_esp_sha_mock_boot_digests(void) { return boot_digests; }

bootloader_sha256_handle_t bootloader_sha256_start(void)
{
    tfw_sha256_init(&engine);
    return &engine;
}

void bootloader_sha256_data(bootloader_sha256_handle_t handle, const void *data, size_t data_len)
{
    if (handle != &engine || data_len > UINT16_MAX
        || (words_only && ((data_len & 3u) != 0u || ((uintptr_t)data & 3u) != 0u))) {
        faults++;
        return;
    }
    tfw_sha256_update(&engine, data, (uint16_t)data_len);
}

void bootloader_sha256_finish(bootloader_sha256_handle_t handle, uint8_t *digest)
{
    if (handle != &engine) {
        faults++;
        return;
    }
    if (digest != NULL) {
        tfw_sha256_final(&engine, digest);
        boot_digests++;
    }
}

void ets_sha_enable(void) { enabled = 1; }
void ets_sha_disable(void) { enabled = 0; }

void ets_sha_init(SHA_CTX *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    tfw_sha256_init(&engine);
    rom_partial = 0;
}

void ets_sha_update(SHA_CTX *ctx, enum SHA_TYPE type, const uint8_t *input, size_t input_bits)
{
    rom_updates++;
    if (!enabled || type != SHA2_256 || (input_bits & 7u) != 0u || rom_partial
        || ((uintptr_t)input & 3u) != 0u || input_bits / 8u > UINT16_MAX) {
        faults++;
        return;
    }
    if ((input_bits & 511u) != 0u) {
        rom_partial = 1;
    }
    ctx->start = true;
    tfw_sha256_update(&engine, input, (uint16_t)(input_bits / 8u));
}

void ets_sha_finish(SHA_CTX *ctx, enum SHA_TYPE type, uint8_t *output)
{
    if (!enabled || type != SHA2_256) {
        faults++;
    }
    tfw_sha256_final(&engine, output);
    memset(ctx, 0, sizeof *ctx);
}
