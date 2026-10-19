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
 *   3. verify it, gate it and derive the CDI   THIS FILE
 *   4. bootloader_utility_load_boot_image()    ESP-IDF, verifies again and runs it
 *
 * Step 3 calls esp_image_verify, which under secure boot v2 checks the
 * image's RSA signature against the key digest in eFuse -- ESP-IDF's check,
 * not a copy of it -- and returns the SHA-256 digest of the image it
 * verified. That digest is the measurement. Then the ES256 gate (l0_gate.c):
 * trailer v1, which the build appends after the image's digest and inside the
 * span secure boot v2 signs, bound to this board, at or above the floor,
 * naming that measurement, signed by the image key whose hash tfw_plat pins
 * (docs/multi-arch/esp32-p4-design.md section 2.2). The CDI is HMAC(UDS, measurement)
 * computed by the HMAC peripheral from the eFuse key block whose purpose is
 * HMAC_UP; the UDS never reaches software. The result goes into the handoff
 * block (boot/boot_handoff.c, the AVR's encoder) in RTC-retained memory, where
 * the application's src/core/handoff.c takes it and clears it.
 *
 * A slot on its first trial boot -- the active OTA data entry PENDING_VERIFY
 * after OTA_ACTIVATE, under CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE -- must also
 * carry an authorization: the tfw_auth record, the image key's ES256 signature
 * over OTA_ACTIVATE's Sig_structure for this trailer's measurement and version
 * (tfw_l0_authorized). Without it the slot is refused like any other and the
 * fallback runs the previous image; ESP-IDF marks the entry ABORTED at the
 * next reset. Under CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK the trailer's
 * image_version must also equal the secure version ESP-IDF read from the
 * image, so the two floors are one number.
 *
 * A refusal is never a handoff. A slot that fails either check gets no CDI
 * and no tier and is not given to the loader; the next slot in ESP-IDF's own
 * fallback order is checked the same way, and with none passing nothing runs,
 * as when no slot verifies. No CDI exists for an image nobody verified.
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
#include "l0_gate.h"

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

/* The image's length up to and including its appended SHA-256: the header,
 * each segment with its header, the checksum byte padded to 16, then the
 * digest (ESP-IDF, API Reference > System API > App Image Format). From the
 * segments rather than data->image_len, which under secure boot v2 may also
 * count the padding and the signature block; it must not exceed it. */
static uint32_t digest_end(const esp_image_metadata_t *d)
{
    uint32_t end = (uint32_t)sizeof(esp_image_header_t);
    int      i;

    for (i = 0; i < d->image.segment_count && i < ESP_IMAGE_MAX_SEGMENTS; i++) {
        end += (uint32_t)sizeof(esp_image_segment_header_t) + d->segments[i].data_len;
    }
    return ((end + 16u) & ~15u) + TFW_BOOT_MEASURE_LEN;
}

/*
 * 1 when the slot ESP-IDF just chose is on its first boot after OTA_ACTIVATE.
 * With rollback on, the selection moves the active otadata entry from NEW to
 * ESP_OTA_IMG_PENDING_VERIFY and writes it, and marks an entry it finds
 * already PENDING_VERIFY -- a trial that reset unconfirmed -- ABORTED and
 * chooses again (bootloader_utility.c, bootloader_utility_get_selected_boot_
 * partition). So an active entry PENDING_VERIFY here, naming this slot, is
 * the one boot that must carry the activate's authorization.
 */
static int on_trial(const bootloader_state_t *bs, int index)
{
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_ota_select_entry_t two[2];
    int                    active;

    if (index < 0 || bs->app_count == 0u || bs->ota_info.size == 0u
        || bootloader_common_read_otadata(&bs->ota_info, two) != ESP_OK) {
        return 0;
    }
    active = bootloader_common_get_active_otadata(two);
    return active >= 0 && two[active].ota_state == ESP_OTA_IMG_PENDING_VERIFY
           && (int)((two[active].ota_seq - 1u) % bs->app_count) == index;
#else
    (void)bs;
    (void)index;
    return 0;
#endif
}

/* Design section 2.2, steps 1 to 7, on one slot, and on a trial the
 * activate's authorization. TFW_OK fills the measurement and the trailer's
 * (signed) version. Every refusal is logged once. */
static tfw_status_t check_slot(const esp_partition_pos_t *pos, int trial, uint8_t measurement[32],
                               uint32_t *version)
{
    uint8_t              rec[TFW_BOOT_AUTH_LEN];
    esp_image_metadata_t data;
    tfw_boot_trailer_t   tr;
    tfw_status_t         st;
    uint32_t             len;

    memset(&data, 0, sizeof data);
    if (esp_image_verify(ESP_IMAGE_VERIFY, pos, &data) != ESP_OK) {
        ESP_LOGE(TAG, "app at 0x%" PRIx32 " failed verification: no CDI", pos->offset);
        return TFW_ERR_CRYPTO;
    }
    len = digest_end(&data);
    if (!data.image.hash_appended || len > data.image_len) {
        ESP_LOGE(TAG, "app at 0x%" PRIx32 " has no appended digest to bind: refused, no CDI",
                 pos->offset);
        return TFW_ERR_RANGE;
    }
    /* The floor is the eFuse secure version (design 2.3). */
    st = tfw_l0_gate(pos->offset, pos->size, len, data.image_digest,
                     esp_efuse_read_secure_version(), &tr);
#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /* One number, two places: the trailer's signed image_version and the
     * secure_version ESP-IDF's anti-rollback holds against the same eFuse
     * floor. The build stamps both from ESP_IMAGE_VERSION; an image where they
     * differ would have the two floors disagree, and is refused. */
    if (st == TFW_OK && tr.image_version != data.secure_version) {
        st = TFW_ERR_PROTECTED_REGION;
    }
#endif
    if (st != TFW_OK) {
        ESP_LOGE(TAG, "app at 0x%" PRIx32 " refused by the trailer gate (status %d): no CDI",
                 pos->offset, (int)st);
        memset(&data, 0, sizeof data);
        return st;
    }
    if (trial) {
        st = tfw_l0_auth_read(rec);
        if (st == TFW_OK) {
            st = tfw_l0_authorized(&tr, rec);
        }
        if (st != TFW_OK) {
            ESP_LOGE(TAG, "app at 0x%" PRIx32 " on trial has no activate authorization (status %d): no CDI",
                     pos->offset, (int)st);
            memset(&data, 0, sizeof data);
            return st;
        }
        ESP_LOGI(TAG, "app at 0x%" PRIx32 " on trial, activate authorized", pos->offset);
    }
    memcpy(measurement, data.image_digest, 32u);
    *version = tr.image_version;
    memset(&data, 0, sizeof data);
    return TFW_OK;
}

/* One slot through the whole gate and, passed, its handoff: step 9, the CDI,
 * only after every check. Returns 1 when the slot may be loaded. */
static int gate_and_hand_off(const bootloader_state_t *bs, int index, int trial)
{
    static uint8_t             block[TFW_BOOT_HANDOFF_LEN];
    const esp_partition_pos_t *pos = slot(bs, index);
    uint8_t                    measurement[32];
    uint8_t                    cdi[32];
    uint8_t                    tier = (uint8_t)TFW_CRYPTO_NONE;
    uint32_t                   version = 0u;
    char                       digest_hex[65];

    if (pos->size == 0u) {
        return 0;                       /* no such partition in the table */
    }
    if (check_slot(pos, trial, measurement, &version) != TFW_OK) {
        return 0;
    }
    hexdump(digest_hex, measurement, 32u);
    ESP_LOGI(TAG, "app at 0x%" PRIx32 " verified, measurement %s", pos->offset, digest_hex);
    derive_cdi(measurement, cdi, &tier);

    tfw_boot_handoff_build(block, tier, cdi, measurement, version, tfw_boot_reset_cause());
    memset(cdi, 0, sizeof cdi);
    retain_mem_prepare(pos);
    tfw_boot_handoff_write(block, (uint8_t)sizeof block);
    memset(block, 0, sizeof block);
    return 1;
}

/* ESP-IDF's own fallback order, bootloader_utility_load_boot_image's: the
 * chosen slot, then down to the factory app, then up; the test app alone.
 * The first slot that passes is the one the loader is given, and the loader
 * tries the slot it is given first. A refused slot is never given to it: it
 * would verify the RSA signature, find it good and run the image.
 *
 * Only the chosen slot can be on trial. Refused there -- an activate nobody
 * authorized -- the fallback boots the slot that ran before, and the next
 * reset finds the entry still PENDING_VERIFY and ESP-IDF marks it ABORTED. */
static int first_passing(const bootloader_state_t *bs, int index)
{
    int trial = on_trial(bs, index);
    int i;

    if (index == TEST_APP_INDEX) {
        return gate_and_hand_off(bs, index, 0) ? index : INVALID_INDEX;
    }
    for (i = index; i >= FACTORY_INDEX; i--) {
        if (gate_and_hand_off(bs, i, i == index && trial)) {
            return i;
        }
    }
    for (i = index + 1; i < (int)bs->app_count; i++) {
        if (gate_and_hand_off(bs, i, 0)) {
            return i;
        }
    }
    return INVALID_INDEX;
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
    index = first_passing(&bs, index);
    if (index == INVALID_INDEX) {
        /* What the loader does when no slot verifies: nothing runs. */
        ESP_LOGE(TAG, "no app passed the gate");
        bootloader_reset();
    }
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
