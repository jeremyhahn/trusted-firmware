/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * LAYER 0, inside ESP-IDF's second-stage bootloader.
 *
 * This replaces the bootloader's entry, call_start_cpu0, with ESP-IDF's own
 * steps in ESP-IDF's own order (the bootloader_override example's shape) and
 * one step added between choosing the application and loading it:
 *
 *   1. bootloader_init()                       ESP-IDF
 *   2. choose the boot partition               ESP-IDF (OTA data, rollback)
 *   3. verify it and derive the CDI            THIS FILE
 *   4. bootloader_utility_load_boot_image()    ESP-IDF, verifies again and runs it
 *
 * Step 3 calls esp_image_verify, which under secure boot v2 checks the
 * image's RSA signature against the key digest in eFuse -- ESP-IDF's check,
 * not a copy of it -- and returns the SHA-256 digest of the image it
 * verified. That digest is the measurement. The CDI is HMAC(UDS, measurement)
 * computed by the HMAC peripheral from the eFuse key block whose purpose is
 * HMAC_UP; the UDS never reaches software. The result goes into the handoff
 * block (boot/boot_handoff.c, the AVR's encoder) in RTC-retained memory, where
 * the application's src/core/handoff.c takes it and clears it.
 *
 * A refusal is never a handoff. An image that fails verification gets an
 * all-zero CDI and its tier, ESP-IDF then refuses to run it in step 4 and
 * falls back to another slot, and no CDI exists for an image nobody verified.
 * A part with no HMAC peripheral (the original ESP32) hands off the
 * measurement under the none tier, with no CDI.
 *
 * Verifying twice -- here and in step 4 -- costs one more pass over the image
 * at boot. The alternative is a hook inside ESP-IDF's loader between its
 * verify and its jump, which ESP-IDF does not offer; re-implementing the
 * loader to make one is what this port does not do.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_crc.h"
#include "esp_efuse.h"
#include "esp_image_format.h"
#include "bootloader_init.h"
#include "bootloader_common.h"
#include "bootloader_utility.h"
#include "soc/soc_caps.h"
#if SOC_HMAC_SUPPORTED
#include "rom/hmac.h"
#endif

#include "tfw/boot.h"
#include "tfw/types.h"
#include "boot_measure.h"
#include "boot_hal.h"

static const char *TAG = "tfw-l0";

static void hexdump(char *out, const uint8_t *in, size_t n)
{
    static const char d[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

static const esp_partition_pos_t *slot(const bootloader_state_t *bs, int index)
{
    if (index == FACTORY_INDEX) {
        return &bs->factory;
    }
    if (index == TEST_APP_INDEX) {
        return &bs->test;
    }
    return &bs->ota[index];
}

/* CDI = HMAC(UDS, measurement) in the HMAC peripheral. On return *tier says
 * whether cdi holds one: TFW_CRYPTO_NONE, with cdi zeroed, on any failure and
 * on a part without the peripheral (the esp32, and the host lint's view). A part
 * without it is a configuration rather than an error, so there is no status
 * that one build could only ever see fail. */
static void derive_cdi(const uint8_t measurement[32], uint8_t cdi[32], uint8_t *tier)
{
#if SOC_HMAC_SUPPORTED
    esp_efuse_block_t block;

    *tier = (uint8_t)TFW_CRYPTO_NONE;
    memset(cdi, 0, 32u);
    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &block)) {
        ESP_LOGW(TAG, "no HMAC_UP key in eFuse: this part has no UDS");
        return;
    }
    ets_hmac_enable();
    if (ets_hmac_calculate_message((ets_efuse_block_t)block, measurement, 32u, cdi) != 0) {
        ets_hmac_disable();
        memset(cdi, 0, 32u);
        ESP_LOGE(TAG, "HMAC peripheral refused key block %d", (int)block);
        return;
    }
    ets_hmac_disable();
    *tier = (uint8_t)TFW_CRYPTO_SECURE_ELEMENT;
    ESP_LOGI(TAG, "cdi from eFuse BLOCK%d through the HMAC peripheral", (int)block);
#else
    (void)measurement;
    *tier = (uint8_t)TFW_CRYPTO_NONE;
    memset(cdi, 0, 32u);
#endif
}

/*
 * Make ESP-IDF's retained record one the loader will keep, before the handoff
 * goes into its custom area. The loader, just before it jumps, zeroes the
 * whole record -- custom area included -- unless its CRC checks, and a header
 * of all zeros is invalid by design: its CRC is 0xFFFFFFFF, the value
 * bootloader_common_loader.c's is_retain_mem_valid() refuses. Both are what a
 * cold boot leaves. So this applies the loader's own rule first (an invalid
 * record is reset), then records the slot just measured, which makes the
 * header non-zero and its CRC one the loader accepts. The CRC span is the
 * header up to `custom`: CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_IN_CRC is off
 * (mk/arch/esp32/sdkconfig.defaults), as the handoff changes under it.
 */
static void retain_mem_prepare(const esp_partition_pos_t *pos)
{
    rtc_retain_mem_t   *m = bootloader_common_get_rtc_retain_mem();
    esp_partition_pos_t booted = *pos;
    uint32_t crc = esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)m, offsetof(rtc_retain_mem_t, custom));

    if (crc != m->crc || m->crc == UINT32_MAX) {
        bootloader_common_reset_rtc_retain_mem();
    }
    bootloader_common_update_rtc_retain_mem(&booted, false);
}

static void measure_and_hand_off(const bootloader_state_t *bs, int index)
{
    static uint8_t       block[TFW_BOOT_HANDOFF_LEN];
    esp_image_metadata_t data;
    uint8_t              cdi[32];
    uint8_t              tier = (uint8_t)TFW_CRYPTO_NONE;
    const esp_partition_pos_t *pos = slot(bs, index);

    memset(cdi, 0, sizeof cdi);
    memset(&data, 0, sizeof data);
    if (esp_image_verify(ESP_IMAGE_VERIFY, pos, &data) != ESP_OK) {
        /* The loader falls back to another slot, which this layer did not
         * measure, so the handoff carries no tier: an app it boots finds no
         * CDI rather than one derived for an image that is not running. */
        ESP_LOGE(TAG, "app at 0x%" PRIx32 " failed verification: no CDI", pos->offset);
        memset(data.image_digest, 0, sizeof data.image_digest);
    } else {
        char digest_hex[65];

        hexdump(digest_hex, data.image_digest, 32u);
        ESP_LOGI(TAG, "app at 0x%" PRIx32 " verified, measurement %s", pos->offset, digest_hex);
        derive_cdi(data.image_digest, cdi, &tier);
    }

    tfw_boot_handoff_build(block, tier, cdi, data.image_digest, data.secure_version,
                           tfw_boot_reset_cause());
    memset(cdi, 0, sizeof cdi);
    retain_mem_prepare(pos);
    tfw_boot_handoff_write(block, (uint8_t)sizeof block);
    memset(block, 0, sizeof block);
}

void __attribute__((noreturn)) call_start_cpu0(void)
{
    bootloader_state_t bs = { 0 };
    int                index;

    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }
    if (!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "no partition table");
        bootloader_reset();
    }
    index = bootloader_utility_get_selected_boot_partition(&bs);
    if (index == INVALID_INDEX) {
        bootloader_reset();
    }
    measure_and_hand_off(&bs, index);
    bootloader_utility_load_boot_image(&bs, index);
}

#if CONFIG_LIBC_NEWLIB
/* The bootloader_override example's: newlib functions linked into the
 * bootloader need a reentrancy structure, and there is only the global one. */
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif
