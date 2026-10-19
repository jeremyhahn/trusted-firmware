/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The controls of test/mock/esp_ota_mock.c: two app slots in RAM, otadata's
 * state for each, the eFuse secure version, and a failure to inject at each
 * esp_ota call.
 *
 * Like ESP-IDF, the mock refuses what the real calls refuse -- a first byte
 * that is not the image magic at esp_ota_write, a write past the slot, an
 * image whose check fails at esp_ota_end -- so a test sees the codes the part
 * returns. Its image check is a stand-in for esp_image_verify's segment walk:
 * the u32 at 0x04 is where the image ends (MOCK_ESP_O_END), and its last 32
 * bytes are the SHA-256 of the rest -- ESP-IDF's appended digest. What follows
 * that end in the slot (the trailer) is outside the check, as on the part.
 * Any call on a handle that is not open (one aborted or ended already) is
 * counted in `stale` and must stay 0.
 *
 * It also stands in for src/hal/esp32/hal_eeprom.c's tfw_esp_part_write,
 * keeping the last record written to TFW_ESP_PART_AUTH.
 */
#ifndef TFW_MOCK_ESP_OTA_MOCK_H
#define TFW_MOCK_ESP_OTA_MOCK_H

#include <stdint.h>
#include "esp_ota_ops.h"
#include "tfw/boot.h"
#include "tfw/status.h"

#define MOCK_ESP_SLOT_LEN   4096u
#define MOCK_ESP_IMG_MAGIC  0xE9u
#define MOCK_ESP_O_END      0x04u   /* the image's end, digest included */

typedef struct {
    uint8_t              flash[2][MOCK_ESP_SLOT_LEN];
    uint32_t             len[2];       /* the image each slot holds */
    esp_ota_img_states_t state[2];     /* otadata; UNDEFINED = no entry */
    uint8_t              running;      /* 0 or 1 */
    uint8_t              no_next;      /* no update partition at all */
    uint32_t             secure_version;
    esp_err_t            begin_err;    /* returned instead, when set */
    esp_err_t            write_err;
    esp_err_t            end_err;
    esp_err_t            metadata_err;
    esp_err_t            set_boot_err;
    esp_err_t            mark_err;
    tfw_status_t         auth_err;     /* tfw_esp_part_write's answer, when set */
    size_t               begin_size;   /* the image_size the last begin asked */
    unsigned             begins, writes, ends, aborts, set_boots, marks, auth_writes, stale;
    uint8_t              boot_slot;    /* the last slot set_boot named */
    uint8_t              open;         /* a handle is held */
    uint8_t              auth[TFW_BOOT_AUTH_LEN];   /* the last tfw_auth record */
} mock_esp_t;

extern mock_esp_t mock_esp;

/* Both slots erased, slot 0 running a VALID image of `len` bytes built by
 * mock_esp_image, no handle open, nothing injected. */
void mock_esp_reset(void);

/* A valid image of `len` bytes into buf: the magic, its end at 0x04, an app
 * description at 0x20 carrying `secure_version`, a body, and the trailing
 * digest. */
void mock_esp_image(uint8_t *buf, uint32_t len, uint32_t secure_version);

/* The partition record of slot i, as esp_ota hands it out. */
const esp_partition_t *mock_esp_slot(uint8_t i);

#endif
