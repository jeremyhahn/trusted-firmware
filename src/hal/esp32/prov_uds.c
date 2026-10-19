/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32's half of eFuse-UDS provisioning (src/caps/provision_uds.h):
 * the hmac-p256 tier's UDS is an eFuse key block with purpose HMAC_UP, the
 * block the bootloader's layer 0 (boot/hal/esp32/boot_l0.c) and the app's
 * DeviceID (crypto_hmac_p256.c) both find by that purpose. Linked only into
 * a PROVISION=1 image of the tier (mk/arch/esp32.mk ARCH_PROV_SRCS_hmac-p256).
 * docs/multi-arch/esp32-p4-design.md section 2.4.
 *
 * THE NOISE SOURCE IS OFF WHEN THE APP RUNS. ESP-IDF v5.5.5's second-stage
 * bootloader calls bootloader_random_disable() unconditionally before it
 * jumps to the app (components/bootloader_support/src/bootloader_utility.c:792),
 * and esp_fill_random() is only true random while a source runs
 * (docs/en/api-reference/system/random.rst:41). So the draw below turns the
 * source on itself, for exactly its own length: bootloader_random_enable()
 * starts the SAR ADC noise source, and on the P4 from chip revision 3.0 the
 * TRNG beside it (bootloader_random_esp32p4.c:26 and :69). That function is
 * unsafe while another driver holds the ADC (bootloader_random.h:33), so the
 * ADC1 lock is taken first and a held lock is TFW_ERR_BUSY, not a weaker draw.
 */

#include "esp_efuse.h"
#include "esp_hmac.h"
#include "esp_log.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "esp_private/adc_share_hw_ctrl.h"

#include "caps/provision_uds.h"

static const char *TAG = "tfw-prov";

static int find_uds(esp_efuse_block_t *block)
{
    return esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, block) ? 1 : 0;
}

int tfw_prov_uds_present(void)
{
    esp_efuse_block_t block;

    return find_uds(&block);
}

tfw_status_t tfw_prov_uds_random(uint8_t uds[32])
{
    if (adc_lock_try_acquire(ADC_UNIT_1) != ESP_OK) {
        return TFW_ERR_BUSY;
    }
    bootloader_random_enable();
    esp_fill_random(uds, 32u);
    bootloader_random_disable();
    (void)adc_lock_release(ADC_UNIT_1);
    return TFW_OK;
}

tfw_status_t tfw_prov_uds_burn(const uint8_t uds[32])
{
    esp_efuse_block_t block = esp_efuse_find_unused_key_block();
    esp_efuse_block_t found;
    esp_err_t         err;

    if (block == EFUSE_BLK_KEY_MAX) {
        ESP_LOGE(TAG, "prov: no unused eFuse key block for the UDS");
        return TFW_ERR_NO_SLOT;
    }
    /* esp_efuse_write_key sets the purpose, read protection for HMAC_UP and
     * write protection of the key and its purpose in one batch
     * (components/efuse/src/efuse_controller/keys/with_key_purposes/
     * esp_efuse_api_key.c:270). */
    err = esp_efuse_write_key(block, ESP_EFUSE_KEY_PURPOSE_HMAC_UP, uds, 32u);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prov: eFuse BLOCK%d burn failed: %s", (int)block, esp_err_to_name(err));
        return TFW_ERR_CRYPTO;
    }
    /* The burn is only done when the part says so: the block is the one
     * layer 0 will find, and software can neither read nor rewrite it. */
    if (!find_uds(&found) || found != block ||
        !esp_efuse_get_key_dis_read(block) || !esp_efuse_get_key_dis_write(block)) {
        ESP_LOGE(TAG, "prov: eFuse BLOCK%d is not a protected HMAC_UP key after the burn", (int)block);
        return TFW_ERR_CRYPTO;
    }
    ESP_LOGI(TAG, "prov: UDS burned into eFuse BLOCK%d, read- and write-protected", (int)block);
    return TFW_OK;
}

tfw_status_t tfw_prov_uds_hmac(const uint8_t *msg, size_t len, uint8_t out[32])
{
    esp_efuse_block_t block;

    if (!find_uds(&block)) {
        return TFW_ERR_CRYPTO;
    }
    if (esp_hmac_calculate((hmac_key_id_t)(block - EFUSE_BLK_KEY0), msg, len, out) != ESP_OK) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}
