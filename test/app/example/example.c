/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The example application of docs/application-sdk/user.md: a heartbeat on pin
 * 13, toggled every 500 ms. It is built as an application repository builds
 * one -- against include/tfw and nothing under src/ -- by test_app_example and
 * by `make app-sdk-check`, which builds it from a copy outside this tree.
 */
#include "tfw/app.h"
#include "tfw/board.h"
#include "tfw/hal.h"

#define HEARTBEAT_CAPS (TFW_APP_CAP(TFW_CAPBIT_GPIO))
TFW_APP_STATIC_CHECK(HEARTBEAT_CAPS, TFW_APP_OTA_NONE);

const tfw_app_descriptor_t tfw_app_descriptor TFW_PROGMEM = {
    .magic = TFW_APP_MAGIC, .abi = TFW_APP_ABI, .ota_slots = TFW_APP_OTA_NONE,
    .version_major = 1u, .required_caps = HEARTBEAT_CAPS, .name = "heartbeat",
};

static tfw_pinref_t led;
static tfw_ms_t     next;
static uint8_t      level;

tfw_status_t tfw_app_init(void)
{
    tfw_status_t st = tfw_board_pin(13u, &led);
    if (st != TFW_OK) {
        return st;
    }
    return tfw_hal_gpio_mode(&led, TFW_PIN_OUTPUT);
}

void tfw_app_run(tfw_ms_t now_ms)
{
    if ((int32_t)(now_ms - next) < 0) {
        return;                      /* not yet: return, never wait */
    }
    next  = now_ms + 500u;
    level = (uint8_t)!level;
    (void)tfw_hal_gpio_write(&led, level);
}

void tfw_app_shutdown(void)        { (void)tfw_hal_gpio_write(&led, 0u); }
void tfw_app_before_update(void)   {}
tfw_status_t tfw_app_after_update(void) { return TFW_OK; }

/* A fixed value, E0 E1 .. FF, that the cross-implementation test expects in the
 * alias certificate. A real application supplies what its verifier registered:
 * a digest of its configuration, say. One that has nothing to say returns
 * TFW_ERR_UNSUPPORTED. */
tfw_status_t tfw_app_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    for (uint8_t i = 0u; i < TFW_APP_EVIDENCE_LEN; i++) {
        out[i] = (uint8_t)(0xE0u + i);
    }
    return TFW_OK;
}
