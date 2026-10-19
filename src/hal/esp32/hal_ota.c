/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OTA session on esp_ota: OTA_BEGIN, OTA_DATA, OTA_END, OTA_STATUS,
 * OTA_INFO, OTA_ACTIVATE and OTA_CONFIRM, and the trial's state (issue #48,
 * docs/multi-arch/esp32-p4-design.md section 2.3, slices 4 and 5).
 *
 * ESP-IDF owns the slots, otadata, the image check and the rollback (L2): the
 * image is written into the OTA partition that is not running, through
 * esp_ota; esp_ota_set_boot_partition activates it; ESP-IDF's bootloader
 * (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) boots it once in PENDING_VERIFY and
 * falls back unless esp_ota_mark_app_valid_cancel_rollback ran first.
 *
 *   OTA_BEGIN  [total_len:4]  the slot esp_ota_get_next_update_partition names,
 *              opened with OTA_WITH_SEQUENTIAL_WRITES: each sector is erased as
 *              the stream reaches it, so no command waits on a 6 MB erase.
 *              TFW_ERR_RANGE for a length that is no larger than a trailer or
 *              larger than the slot; TFW_ERR_BAD_STATE while the running image
 *              is ESP_OTA_IMG_PENDING_VERIFY (on trial, as tfw-boot refuses a
 *              session on a swapped 2560); TFW_ERR_UNSUPPORTED with no slot.
 *   OTA_DATA   [offset:4][bytes] in order, as the AVR stager takes them.
 *   OTA_END    every byte arrived, then esp_ota_end, which also runs
 *              ESP-IDF's image check: a bad image fails HERE with
 *              TFW_ERR_CRYPTO, the verify-failure status, where the AVR's
 *              stager would already have refused it -- a stronger refusal, not
 *              a different meaning (docs/ota/api.md).
 *   OTA_STATUS [state:1][received:4] the session's own counters.
 *   OTA_INFO   [slot:1] slot 0 is the running image, slot 1 the other.
 *   OTA_ACTIVATE [mode:1] after OTA_AUTH, ota_activate.c's wire: the ended
 *              image's trailer checked as layer 0 will check it, short of the
 *              ES256 signatures, which are layer 0's (tfw_stage_authorize);
 *              the authorization written to tfw_auth for layer 0 to verify at
 *              the trial boot; then esp_ota_set_boot_partition. Mode 1 resets.
 *   OTA_CONFIRM after OTA_AUTH, on trial only: esp_ota_mark_app_valid_cancel_
 *              rollback. As on the AVR (#125) the authorization is required
 *              and not verified here.
 *
 * The floor is the eFuse secure version, which this file reads and never
 * writes. Under CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK, ESP-IDF's confirm raises
 * it to the image's secure version, which the build stamps from the trailer's
 * image_version (mk/arch/esp32-targets.mk).
 */

#include <string.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_efuse.h"

#include "tfw/boot.h"
#include "tfw/config.h"
#include "boot_image.h"
#include "core/ota_stage.h"
#include "core/ota_ctl.h"
#include "core/ota_auth.h"
#include "core/ota_trial.h"
#include "core/auth_session.h"
#include "caps/sys.h"
#include "hal_esp32.h"

#define INFO_FLAG_FELL_BACK 0x01u
#define INFO_FLAG_TRIAL     0x04u

static uint8_t                state;
static uint32_t               total;
static uint32_t               received;
static esp_ota_handle_t       handle;
static const esp_partition_t *slot;

static tfw_status_t from_esp(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_OTA_VALIDATE_FAILED:        return TFW_ERR_CRYPTO;
    case ESP_ERR_OTA_ROLLBACK_INVALID_STATE: return TFW_ERR_BAD_STATE;
    case ESP_ERR_INVALID_SIZE:               return TFW_ERR_RANGE;
    default:                                 return TFW_ERR_INTERNAL;
    }
}

/* A session that ends early gives its handle back: esp_ota_abort frees what
 * esp_ota_begin took. esp_ota_end frees it too, even when it fails, so END
 * never calls this after it. */
static tfw_status_t fail(tfw_status_t st)
{
    if (state == TFW_STAGE_RECEIVING) {
        (void)esp_ota_abort(handle);
    }
    state = TFW_STAGE_FAILED;
    return st;
}

static int in_state(const esp_partition_t *p, esp_ota_img_states_t want)
{
    esp_ota_img_states_t s;

    return p != NULL && esp_ota_get_state_partition(p, &s) == ESP_OK && s == want;
}

/* main.c's, at boot: no session. Any handle a session held is given back. */
void tfw_stage_init(void)
{
    if (state == TFW_STAGE_RECEIVING) {
        (void)esp_ota_abort(handle);
    }
    state    = TFW_STAGE_IDLE;
    total    = 0u;
    received = 0u;
    slot     = NULL;
}

tfw_status_t tfw_stage_cmd_begin(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t               len = tfw_read_u32(r);
    const esp_partition_t *next;
    esp_err_t              err;

    (void)w;
    if (in_state(esp_ota_get_running_partition(), ESP_OTA_IMG_PENDING_VERIFY)) {
        return TFW_ERR_BAD_STATE;
    }
    next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (len <= (uint32_t)TFW_BOOT_TRAILER_LEN || len > next->size) {
        return TFW_ERR_RANGE;
    }
    if (state == TFW_STAGE_RECEIVING) {
        (void)esp_ota_abort(handle);   /* a new BEGIN starts over */
    }
    state    = TFW_STAGE_IDLE;
    total    = len;
    received = 0u;
    slot     = next;
    err = esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        return fail(from_esp(err));
    }
    state = TFW_STAGE_RECEIVING;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_data(tfw_reader_t *r, tfw_writer_t *w)
{
    uint32_t       off = tfw_read_u32(r);
    uint8_t        n   = tfw_reader_remaining(r);
    const uint8_t *p   = tfw_read_blob(r, n);
    esp_err_t      err;

    (void)w;
    if (state != TFW_STAGE_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    if (off != received || (uint32_t)n > total - received) {
        return TFW_ERR_RANGE;
    }
    err = esp_ota_write(handle, p, n);
    if (err != ESP_OK) {
        return fail(from_esp(err));
    }
    received += n;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_end(tfw_reader_t *r, tfw_writer_t *w)
{
    esp_err_t err;

    (void)r;
    (void)w;
    if (state != TFW_STAGE_RECEIVING) {
        return TFW_ERR_BUSY;
    }
    if (received != total) {
        return fail(TFW_ERR_FRAMING);
    }
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        state = TFW_STAGE_FAILED;   /* the handle is already freed */
        return from_esp(err);
    }
    state = TFW_STAGE_COMPLETE;
    return TFW_OK;
}

tfw_status_t tfw_stage_cmd_status(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)r;
    tfw_write_u8(w, state);
    tfw_write_u32(w, received);
    return TFW_OK;
}

/* The ended image's trailer v1, as layer 0's tfw_l0_gate will read it at the
 * trial boot: just past the digest ESP-IDF appended, bound to this board, at
 * or above the eFuse floor, naming the image's span and that digest. So an
 * image layer 0 would refuse on any of these is refused here, with the same
 * status, before anything is written. The parser's length bound is the board
 * profile's geometry, which layer 0 builds with too. esp_image_get_metadata's
 * image_len ends at the appended digest (esp_image_format.c,
 * process_appended_hash_and_sig); esp_image_verify's would reach past the
 * signature. */
static tfw_status_t check_staged(void)
{
    esp_partition_pos_t  pos = { slot->address, slot->size };
    esp_image_metadata_t md;
    esp_app_desc_t       desc;
    tfw_boot_trailer_t   tr;
    uint8_t              raw[TFW_BOOT_TRAILER_LEN];
    uint8_t              hash[TFW_BOOT_MEASURE_LEN];
    tfw_status_t         st;

    memset(&md, 0, sizeof md);
    if (esp_image_get_metadata(&pos, &md) != ESP_OK || !md.image.hash_appended
        || md.image_len <= TFW_BOOT_MEASURE_LEN || md.image_len > slot->size
        || slot->size - md.image_len < TFW_BOOT_TRAILER_LEN) {
        return TFW_ERR_RANGE;
    }
    if (esp_partition_read(slot, md.image_len, raw, sizeof raw) != ESP_OK
        || esp_partition_get_sha256(slot, hash) != ESP_OK
        || esp_ota_get_partition_description(slot, &desc) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    st = tfw_boot_trailer_parse(raw, &tr);
    if (st == TFW_OK) {
        st = tfw_boot_binding_permit(&tr, (uint8_t)TFW_BOARD_ID);
    }
    if (st == TFW_OK) {
        st = tfw_boot_rollback_permit(tr.image_version, esp_efuse_read_secure_version());
    }
    if (st != TFW_OK) {
        return st;
    }
    if (tr.image_len != md.image_len - TFW_BOOT_MEASURE_LEN
        || memcmp(tr.measurement, hash, TFW_BOOT_MEASURE_LEN) != 0) {
        return TFW_ERR_CRYPTO;
    }
#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /* Layer 0's one-number rule (boot_l0.c): ESP-IDF's floor and the
     * trailer's are the same value or the image is refused. */
    if (tr.image_version != desc.secure_version) {
        return TFW_ERR_PROTECTED_REGION;
    }
#else
    (void)desc;
#endif
    return TFW_OK;
}

/* OTA_ACTIVATE's first half: the ended image checked, then OTA_AUTH's
 * signature stored in tfw_auth (include/tfw/boot.h, TFW_BOOT_AUTH_*) for layer
 * 0, which verifies it with the image key at the trial boot. One record, for
 * whichever slot is activated: it names its image's measurement and version,
 * so an earlier activate's record authorizes nothing else. */
tfw_status_t tfw_stage_authorize(const uint8_t sig[TFW_OTA_AUTH_SIG_LEN], uint8_t mode)
{
    uint8_t      rec[TFW_BOOT_AUTH_LEN];
    tfw_status_t st;

    if (state != TFW_STAGE_COMPLETE
        || in_state(esp_ota_get_running_partition(), ESP_OTA_IMG_PENDING_VERIFY)) {
        return TFW_ERR_BAD_STATE;
    }
    st = check_staged();
    if (st != TFW_OK) {
        return st;
    }
    memset(rec, 0, sizeof rec);
    rec[TFW_BOOT_AUTH_O_MAGIC]      = (uint8_t)TFW_BOOT_AUTH_MAGIC;
    rec[TFW_BOOT_AUTH_O_MAGIC + 1u] = (uint8_t)(TFW_BOOT_AUTH_MAGIC >> 8);
    rec[TFW_BOOT_AUTH_O_MAGIC + 2u] = (uint8_t)(TFW_BOOT_AUTH_MAGIC >> 16);
    rec[TFW_BOOT_AUTH_O_MAGIC + 3u] = (uint8_t)(TFW_BOOT_AUTH_MAGIC >> 24);
    rec[TFW_BOOT_AUTH_O_MODE]       = mode;
    memcpy(&rec[TFW_BOOT_AUTH_O_SIG], sig, TFW_OTA_AUTH_SIG_LEN);
    return tfw_esp_part_write(TFW_ESP_PART_AUTH, 0u, rec, TFW_BOOT_AUTH_LEN);
}

/* The second half: ESP-IDF's own activate, which verifies the image again and
 * writes otadata; the next reset boots it once, in PENDING_VERIFY. */
tfw_status_t tfw_stage_activate(void)
{
    if (state != TFW_STAGE_COMPLETE || esp_ota_set_boot_partition(slot) != ESP_OK) {
        return TFW_ERR_BAD_STATE;
    }
    return TFW_OK;
}

/* RESET's question (#51): this session's OTA_ACTIVATE named its slot in
 * otadata, and no OTA_BEGIN has started over since. */
uint8_t tfw_stage_install_pending(void)
{
    return (uint8_t)(state == TFW_STAGE_COMPLETE && slot != NULL
                     && esp_ota_get_boot_partition() == slot);
}

/* OTA_ACTIVATE [mode:1], src/core/ota_activate.c's wire. That file reads
 * tfw-boot's platform block, which this port has not got: here the trial is
 * the running image's otadata state, which tfw_stage_authorize refuses. */
tfw_status_t tfw_otactl_cmd_activate(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t        mode = tfw_read_u8(r);
    const uint8_t *sig;
    tfw_status_t   st;

    (void)w;
    if (mode > 1u) {
        return TFW_ERR_RANGE;
    }
    sig = tfw_ota_auth_take();
    if (sig == NULL) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    st = tfw_auth_gate();
    if (st == TFW_OK) {
        st = tfw_stage_authorize(sig, mode);
    }
    if (st != TFW_OK) {
        return st;
    }
    if (tfw_stage_activate() != TFW_OK) {
        return TFW_ERR_BAD_STATE;
    }
    if (mode == 1u) {
        tfw_sys_reset_soon();
    }
    return TFW_OK;
}

/* The trial is ESP-IDF's: the running image is PENDING_VERIFY until it is
 * confirmed (src/core/ota_trial.c resets at the deadline while it is). */
uint8_t tfw_ota_trial_on(void)
{
    return (uint8_t)in_state(esp_ota_get_running_partition(), ESP_OTA_IMG_PENDING_VERIFY);
}

tfw_status_t tfw_otactl_cmd_confirm(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_status_t st;

    (void)r;
    (void)w;
    if (tfw_ota_auth_take() == NULL) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    st = tfw_auth_gate();
    if (st != TFW_OK) {
        return st;
    }
    if (tfw_ota_trial_on() == 0u) {
        return TFW_ERR_BAD_STATE;
    }
    /* Under ANTI_ROLLBACK this also raises the eFuse floor to the image's
     * secure version (esp_ota_ops.c): ESP-IDF's write, not this file's. */
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

/* The image in p: its secure_version and the digest ESP-IDF appended to it,
 * the measurement layer 0 logs. */
static tfw_status_t describe(const esp_partition_t *p, uint32_t *version,
                             uint8_t hash[TFW_BOOT_MEASURE_LEN])
{
    esp_app_desc_t desc;

    if (esp_ota_get_partition_description(p, &desc) != ESP_OK ||
        esp_partition_get_sha256(p, hash) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    *version = desc.secure_version;
    return TFW_OK;
}

tfw_status_t tfw_otactl_cmd_info(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t                which = tfw_read_u8(r);
    const esp_partition_t *run   = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    uint8_t                hash[TFW_BOOT_MEASURE_LEN];
    uint8_t                flags = 0u;
    uint8_t                st;
    uint32_t               version = 0u;

    if (which > 1u) {
        return TFW_ERR_RANGE;
    }
    if (run == NULL) {
        return TFW_ERR_INTERNAL;
    }
    /* The bootloader marks a trial it rolled back from ABORTED, and the app
     * marks one INVALID: either way the other slot fell back. So did a trial
     * still PENDING_VERIFY while this image runs: layer 0 refused it at its
     * one boot (no activate authorization), and ESP-IDF marks it ABORTED at
     * the next reset. */
    if (in_state(other, ESP_OTA_IMG_ABORTED) || in_state(other, ESP_OTA_IMG_INVALID)
        || in_state(other, ESP_OTA_IMG_PENDING_VERIFY)) {
        flags |= INFO_FLAG_FELL_BACK;
    }
    if (in_state(run, ESP_OTA_IMG_PENDING_VERIFY)) {
        flags |= INFO_FLAG_TRIAL;
    }
    memset(hash, 0, sizeof hash);
    if (which == 0u) {
        st = (flags & INFO_FLAG_TRIAL) != 0u ? TFW_BOOT_STATE_TRIAL : TFW_BOOT_STATE_GOOD;
        if (describe(run, &version, hash) != TFW_OK) {
            return TFW_ERR_INTERNAL;
        }
    } else {
        st = state;
        if (state == TFW_STAGE_COMPLETE && describe(slot, &version, hash) != TFW_OK) {
            return TFW_ERR_INTERNAL;
        }
    }
    tfw_write_u8(w, 2u);
    tfw_write_u8(w, 0u);
    tfw_write_u8(w, flags);
    tfw_write_u32(w, esp_efuse_read_secure_version());
    tfw_write_u8(w, st);
    tfw_write_u32(w, version);
    tfw_write_blob(w, hash, TFW_BOOT_MEASURE_LEN);
    return TFW_OK;
}
