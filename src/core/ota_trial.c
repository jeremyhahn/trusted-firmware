/* SPDX-License-Identifier: commercial */

#include <stdint.h>

#include "tfw/config.h"
#include "tfw/hal.h"
#include "app_runner.h"
#include "boot_platform.h"
#include "ota_trial.h"

#define TRIAL_MS ((tfw_ms_t)TFW_OTA_TRIAL_TIMEOUT_S * 1000u)

static uint8_t  armed;      /* TRIAL: the watchdog is ours to pet and the deadline runs */
static tfw_ms_t started;

void tfw_ota_trial_start(void)
{
    armed = 0u;
    if (tfw_ota_trial_on() == 0u) {
        return;
    }
    tfw_hal_watchdog_arm();
    armed   = 1u;
    started = tfw_hal_millis();
}

void tfw_ota_trial_after_init(void)
{
    tfw_app_state_t app;

    if (armed == 0u) {
        return;
    }
    /* Decision 9: the image reports only if init and after_update both return
     * TFW_OK. A failed init, or a descriptor that failed its checks so init
     * never ran, ends the trial at once as a refusal does: tfw-boot finds
     * TRIAL with no confirm request and rolls back. TFW_OK confirms nothing;
     * the deadline runs on for the control plane's OTA_CONFIRM. */
    app = tfw_app_runner_state();
    if (app == TFW_APP_STATE_FAILED || app == TFW_APP_STATE_UNSUPPORTED ||
        tfw_app_runner_after_update() != TFW_OK) {
        tfw_hal_reset();
    }
}

void tfw_ota_trial_poll(void)
{
    if (armed == 0u) {
        return;
    }
    tfw_hal_watchdog_pet();
    if ((tfw_ms_t)(tfw_hal_millis() - started) >= TRIAL_MS) {
        /* Confirmed or not on the AVR (#137), where boot_state stays TRIAL
         * until tfw-boot settles it by OTA_CONFIRM's request or rolls back.
         * On ESP32 a confirm ended the trial, and only an unconfirmed one
         * resets into ESP-IDF's rollback. */
        if (tfw_ota_trial_on() == 0u) {
            armed = 0u;
            return;
        }
        tfw_hal_reset();
    }
}
