/* SPDX-License-Identifier: commercial */

#include <stdint.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "boot_platform.h"
#include "ota_trial.h"

#define TRIAL_MS ((tfw_ms_t)TFW_OTA_TRIAL_TIMEOUT_S * 1000u)

static uint8_t  armed;      /* TRIAL: the watchdog is ours to pet and the deadline runs */
static tfw_ms_t started;

void tfw_ota_trial_start(void)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;

    armed = 0u;
    if (tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN) != TFW_OK
        || tfw_boot_platform_parse(raw, &plat) != TFW_OK
        || plat.boot_state != TFW_BOOT_STATE_TRIAL) {
        return;
    }
    tfw_hal_watchdog_arm();
    armed   = 1u;
    started = tfw_hal_millis();
}

void tfw_ota_trial_poll(void)
{
    if (armed == 0u) {
        return;
    }
    tfw_hal_watchdog_pet();
    if ((tfw_ms_t)(tfw_hal_millis() - started) >= TRIAL_MS) {
        /* Confirmed or not (#137): back into tfw-boot, which finds TRIAL and
         * settles it by OTA_CONFIRM's request or rolls back. */
        tfw_hal_reset();
    }
}
