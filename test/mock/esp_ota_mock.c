/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * esp_ota, esp_partition_get_sha256 and _read, esp_image_get_metadata, the
 * eFuse secure-version read and the tfw_auth write on the host, for
 * src/hal/esp32/hal_ota.c. The controls are in esp32/esp_ota_mock.h.
 */
#include <string.h>
#include "esp_ota_mock.h"
#include "esp_efuse.h"
#include "esp_image_format.h"
#include "hal_esp32.h"
#include "sha256.h"

#define HANDLE 0x5A17u   /* the one handle the mock hands out */

mock_esp_t mock_esp;

static const esp_partition_t slots[2] = {
    { NULL, 0, 0x10, 0x20000u, MOCK_ESP_SLOT_LEN, 4096u, "ota_0", false, false },
    { NULL, 0, 0x11, 0x20000u + MOCK_ESP_SLOT_LEN, MOCK_ESP_SLOT_LEN, 4096u, "ota_1", false, false },
};

static uint8_t  target;   /* the slot the open handle writes */
static uint32_t wrote;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void digest(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    tfw_sha256_t c;

    tfw_sha256_init(&c);
    tfw_sha256_update(&c, p, (uint16_t)n);
    tfw_sha256_final(&c, out);
}

void mock_esp_image(uint8_t *buf, uint32_t len, uint32_t secure_version)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        buf[i] = (uint8_t)(i * 13u + 5u);
    }
    buf[0] = MOCK_ESP_IMG_MAGIC;
    put32(&buf[MOCK_ESP_O_END], len);
    put32(&buf[0x20], ESP_APP_DESC_MAGIC_WORD);
    put32(&buf[0x24], secure_version);
    digest(buf, len - 32u, &buf[len - 32u]);
}

void mock_esp_reset(void)
{
    memset(&mock_esp, 0, sizeof mock_esp);
    memset(mock_esp.flash, 0xFF, sizeof mock_esp.flash);
    mock_esp.state[0] = ESP_OTA_IMG_VALID;
    mock_esp.state[1] = ESP_OTA_IMG_UNDEFINED;
    mock_esp.len[0]   = 1024u;
    mock_esp_image(mock_esp.flash[0], mock_esp.len[0], 1u);
    target = 0u;
    wrote  = 0u;
}

const esp_partition_t *mock_esp_slot(uint8_t i)
{
    return &slots[i];
}

static int slot_of(const esp_partition_t *p)
{
    return p == &slots[0] ? 0 : (p == &slots[1] ? 1 : -1);
}

/* Where slot i's image ends, digest included: what the segment walk finds. */
static uint32_t image_end(uint8_t i)
{
    return get32(&mock_esp.flash[i][MOCK_ESP_O_END]);
}

static int held(esp_ota_handle_t h)
{
    if (!mock_esp.open || h != HANDLE) {
        mock_esp.stale++;
        return 0;
    }
    return 1;
}

const esp_partition_t *esp_ota_get_running_partition(void)
{
    return &slots[mock_esp.running];
}

const esp_partition_t *esp_ota_get_boot_partition(void)
{
    return &slots[mock_esp.boot_slot];
}

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from)
{
    (void)start_from;
    return mock_esp.no_next ? NULL : &slots[1u - mock_esp.running];
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle)
{
    int i = slot_of(partition);

    mock_esp.begins++;
    mock_esp.begin_size = image_size;
    if (i < 0 || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (i == mock_esp.running) {
        return ESP_ERR_OTA_PARTITION_CONFLICT;
    }
    if (mock_esp.state[mock_esp.running] == ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
    }
    if (mock_esp.begin_err != ESP_OK) {
        return mock_esp.begin_err;
    }
    /* Sequential writes erase as they go; any other size erases it all now. */
    if (image_size != OTA_WITH_SEQUENTIAL_WRITES) {
        memset(mock_esp.flash[i], 0xFF, MOCK_ESP_SLOT_LEN);
    }
    mock_esp.len[i] = 0u;
    mock_esp.open   = 1u;
    target          = (uint8_t)i;
    wrote           = 0u;
    *out_handle     = HANDLE;
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    const uint8_t *p = data;

    mock_esp.writes++;
    if (!held(handle)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.write_err != ESP_OK) {
        return mock_esp.write_err;
    }
    if (wrote == 0u && size > 0u && p[0] != MOCK_ESP_IMG_MAGIC) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    if (size > MOCK_ESP_SLOT_LEN - wrote) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&mock_esp.flash[target][wrote], p, size);
    wrote += (uint32_t)size;
    return ESP_OK;
}

/* Frees the handle on every path, as ESP-IDF's does. */
esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    uint8_t  want[32];
    uint32_t n;

    mock_esp.ends++;
    if (!held(handle)) {
        return ESP_ERR_NOT_FOUND;
    }
    mock_esp.open = 0u;
    if (mock_esp.end_err != ESP_OK) {
        return mock_esp.end_err;
    }
    if (wrote <= 32u) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    n = image_end(target);
    if (n <= 32u || n > wrote) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    digest(mock_esp.flash[target], n - 32u, want);
    if (memcmp(want, &mock_esp.flash[target][n - 32u], 32u) != 0) {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    mock_esp.len[target] = wrote;
    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    mock_esp.aborts++;
    if (!held(handle)) {
        return ESP_ERR_NOT_FOUND;
    }
    mock_esp.open = 0u;
    return ESP_OK;
}

/* As ESP-IDF's under CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE: the slot's otadata
 * entry becomes NEW, which its bootloader boots once as PENDING_VERIFY. */
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    int i = slot_of(partition);

    mock_esp.set_boots++;
    if (i < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.set_boot_err != ESP_OK) {
        return mock_esp.set_boot_err;
    }
    mock_esp.boot_slot = (uint8_t)i;
    mock_esp.state[i]  = ESP_OTA_IMG_NEW;
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    mock_esp.marks++;
    if (mock_esp.mark_err != ESP_OK) {
        return mock_esp.mark_err;
    }
    if (mock_esp.state[mock_esp.running] == ESP_OTA_IMG_PENDING_VERIFY) {
        mock_esp.state[mock_esp.running] = ESP_OTA_IMG_VALID;
    }
    return ESP_OK;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *ota_state)
{
    int i = slot_of(partition);

    if (i < 0 || ota_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.state[i] == ESP_OTA_IMG_UNDEFINED) {
        return ESP_ERR_NOT_FOUND;
    }
    *ota_state = mock_esp.state[i];
    return ESP_OK;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *app_desc)
{
    int i = slot_of(partition);

    if (i < 0 || app_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.len[i] == 0u || get32(&mock_esp.flash[i][0x20]) != ESP_APP_DESC_MAGIC_WORD) {
        return ESP_ERR_NOT_FOUND;
    }
    memset(app_desc, 0, sizeof *app_desc);
    app_desc->magic_word     = ESP_APP_DESC_MAGIC_WORD;
    app_desc->secure_version = get32(&mock_esp.flash[i][0x24]);
    return ESP_OK;
}

/* For an app partition, the digest appended to the image it holds. */
esp_err_t esp_partition_get_sha256(const esp_partition_t *partition, uint8_t *sha_256)
{
    int i = slot_of(partition);

    if (i < 0 || sha_256 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.len[i] <= 32u || image_end((uint8_t)i) <= 32u || image_end((uint8_t)i) > mock_esp.len[i]) {
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(sha_256, &mock_esp.flash[i][image_end((uint8_t)i) - 32u], 32u);
    return ESP_OK;
}

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset, void *dst, size_t size)
{
    int i = slot_of(partition);

    if (i < 0 || dst == NULL || src_offset > MOCK_ESP_SLOT_LEN || size > MOCK_ESP_SLOT_LEN - src_offset) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(dst, &mock_esp.flash[i][src_offset], size);
    return ESP_OK;
}

/* No verify, as ESP-IDF's (esp_image_format.c): the header and where the
 * segments end, digest included. */
esp_err_t esp_image_get_metadata(const esp_partition_pos_t *part, esp_image_metadata_t *metadata)
{
    int i = (part != NULL && part->offset == slots[0].address) ? 0
          : (part != NULL && part->offset == slots[1].address) ? 1 : -1;

    if (i < 0 || metadata == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_esp.metadata_err != ESP_OK) {
        return mock_esp.metadata_err;
    }
    if (mock_esp.flash[i][0] != MOCK_ESP_IMG_MAGIC) {
        return ESP_ERR_IMAGE_INVALID;
    }
    memset(metadata, 0, sizeof *metadata);
    metadata->start_addr          = part->offset;
    metadata->image.magic         = MOCK_ESP_IMG_MAGIC;
    metadata->image.hash_appended = 1u;
    metadata->image_len           = image_end((uint8_t)i);
    return ESP_OK;
}

tfw_status_t tfw_esp_part_write(const char *label, uint32_t off, const uint8_t *buf, uint32_t len)
{
    mock_esp.auth_writes++;
    if (mock_esp.auth_err != TFW_OK) {
        return mock_esp.auth_err;
    }
    if (strcmp(label, TFW_ESP_PART_AUTH) != 0 || off != 0u || len != TFW_BOOT_AUTH_LEN) {
        return TFW_ERR_RANGE;
    }
    memcpy(mock_esp.auth, buf, len);
    return TFW_OK;
}

uint32_t esp_efuse_read_secure_version(void)
{
    return mock_esp.secure_version;
}
