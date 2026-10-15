/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#include "app_runner.h"
#include "update_request.h"
#include "tfw/app.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "tfw/progmem.h"

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
