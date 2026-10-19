/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include "app_runner.h"
#include "update_request.h"
#include "tfw/app.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "tfw/progmem.h"
#include "tfw/protocol.h"

static tfw_app_state_t state;
static uint16_t        overruns;

/* 1 when the n bytes at p (in flash) hold a NUL. A name with none would run off
 * the end of its field into the next one when something prints it. */
static uint8_t has_nul(const char *p, uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        if (tfw_pgm_u8(&p[i]) == 0u) {
            return 1u;
        }
    }
    return 0u;
}

/* Every refusal is the same refusal: the application does not start, and the
 * firmware serves on without it (docs/application-sdk/engineering.md). The
 * checks come in the order that document lists them. */
static uint8_t descriptor_ok(void)
{
    const tfw_app_descriptor_t *d = &tfw_app_descriptor;
    uint32_t caps;
    uint8_t  slots;
    uint16_t cfg;

    if (tfw_pgm_u32(&d->magic) != TFW_APP_MAGIC) {
        return 0u;
    }
    if (tfw_pgm_u8(&d->abi) != TFW_APP_ABI) {
        return 0u;
    }
    if (tfw_pgm_u8(&d->name[0]) == 0u || !has_nul(d->name, TFW_APP_NAME_LEN) ||
        !has_nul(d->profile, TFW_APP_PROFILE_LEN)) {
        return 0u;
    }
    caps = tfw_pgm_u32(&d->required_caps);
    if ((caps & ~(uint32_t)TFW_FEATURE_BITMAP) != 0u) {
        return 0u;
    }
    /* A board with no bootloader serves no update path at all, whatever its
     * profile says about slots. */
    slots = (uint8_t)(tfw_update_request_supported() ? TFW_OTA_SLOTS : 0u);
    if (tfw_pgm_u8(&d->ota_slots) > slots) {
        return 0u;
    }
    cfg = tfw_pgm_u16(&d->config_len);
    if ((uint32_t)TFW_APP_CONFIG_BASE + cfg > (uint32_t)TFW_EEPROM_SIZE) {
        return 0u;
    }
    return 1u;
}

void tfw_app_runner_start(void)
{
    overruns = 0u;
    if (!descriptor_ok()) {
        state = TFW_APP_STATE_UNSUPPORTED;
        return;
    }
    state = (tfw_app_init() == TFW_OK) ? TFW_APP_STATE_RUNNING : TFW_APP_STATE_FAILED;
}

void tfw_app_runner_poll(void)
{
    tfw_ms_t start;

    if (state != TFW_APP_STATE_RUNNING) {
        return;
    }
    start = tfw_hal_millis();
    tfw_app_run(start);
    /* Measured, not enforced: abandoning a call needs preemption C cannot
     * unwind, and a watchdog reset would turn a slow application into a reset
     * loop. The count is what a host will read to find the slow one. */
    if ((tfw_ms_t)(tfw_hal_millis() - start) > TFW_APP_RUN_BUDGET_MS && overruns != 0xFFFFu) {
        overruns = (uint16_t)(overruns + 1u);
    }
}

void tfw_app_runner_stop(uint8_t for_update)
{
    if (state != TFW_APP_STATE_RUNNING) {
        return;
    }
    /* Before the hooks, so nothing they cause can call them a second time. */
    state = TFW_APP_STATE_STOPPED;
    if (for_update) {
        tfw_app_before_update();
    }
    tfw_app_shutdown();
}

tfw_app_state_t tfw_app_runner_state(void)
{
    return state;
}

uint16_t tfw_app_runner_overruns(void)
{
    return overruns;
}

tfw_status_t tfw_app_runner_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    uint8_t ev[TFW_APP_EVIDENCE_LEN];

    if (state != TFW_APP_STATE_RUNNING || tfw_app_evidence(ev) != TFW_OK) {
        return TFW_ERR_UNSUPPORTED;
    }
    /* Through a copy, so a refusal that wrote half the buffer leaves out as it was. */
    for (uint8_t i = 0u; i < TFW_APP_EVIDENCE_LEN; i++) {
        out[i] = ev[i];
    }
    return TFW_OK;
}

tfw_status_t tfw_app_runner_after_update(void)
{
    return (state == TFW_APP_STATE_RUNNING) ? tfw_app_after_update() : TFW_OK;
}

/* The reply is the descriptor minus its magic, behind the runner's two
 * fields. Both lengths are pinned here so that growing the descriptor's
 * strings breaks the build rather than the 64-byte payload. */
_Static_assert(TFW_APP_INFO_LEN == 15u + TFW_APP_NAME_LEN + TFW_APP_PROFILE_LEN,
               "APP_INFO is the descriptor minus its magic, plus state and overruns");
_Static_assert(TFW_APP_INFO_LEN <= TFW_MAX_PAYLOAD,
               "APP_INFO must fit one reply");

static void write_pgm_str(tfw_writer_t *w, const char *p, uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        tfw_write_u8(w, tfw_pgm_u8(&p[i]));
    }
}

/* Reported whatever the state, including UNSUPPORTED: a host diagnosing why
 * an application never started needs the descriptor the runner refused. */
tfw_status_t tfw_app_cmd_info(tfw_reader_t *r, tfw_writer_t *w)
{
    const tfw_app_descriptor_t *d = &tfw_app_descriptor;

    (void)r;
    tfw_write_u8(w,  state);
    tfw_write_u16(w, overruns);
    tfw_write_u8(w,  tfw_pgm_u8(&d->abi));
    tfw_write_u8(w,  tfw_pgm_u8(&d->ota_slots));
    tfw_write_u8(w,  tfw_pgm_u8(&d->version_major));
    tfw_write_u8(w,  tfw_pgm_u8(&d->version_minor));
    tfw_write_u16(w, tfw_pgm_u16(&d->version_patch));
    tfw_write_u16(w, tfw_pgm_u16(&d->config_len));
    tfw_write_u32(w, tfw_pgm_u32(&d->required_caps));
    write_pgm_str(w, d->name, TFW_APP_NAME_LEN);
    write_pgm_str(w, d->profile, TFW_APP_PROFILE_LEN);
    return TFW_OK;
}

/* The region is checked against the declared length and the part both: the
 * length is the application's word, and an UNSUPPORTED application -- the one
 * whose length overran the part -- may still call these. */
static tfw_status_t config_span(uint16_t off, uint8_t len)
{
    uint32_t end = (uint32_t)off + len;

    if (end > tfw_pgm_u16(&tfw_app_descriptor.config_len) ||
        (uint32_t)TFW_APP_CONFIG_BASE + end > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    return TFW_OK;
}

tfw_status_t tfw_app_config_read(uint16_t off, uint8_t *buf, uint8_t len)
{
    tfw_status_t st = config_span(off, len);

    if (st != TFW_OK || len == 0u) {
        return st;
    }
    return tfw_hal_eeprom_read((uint16_t)(TFW_APP_CONFIG_BASE + off), buf, len);
}

tfw_status_t tfw_app_config_write(uint16_t off, const uint8_t *buf, uint8_t len)
{
    tfw_status_t st = config_span(off, len);

    if (st != TFW_OK || len == 0u) {
        return st;
    }
    return tfw_hal_eeprom_write((uint16_t)(TFW_APP_CONFIG_BASE + off), buf, len);
}
