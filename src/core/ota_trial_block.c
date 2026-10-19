/* SPDX-License-Identifier: commercial */
/*
 * Whether the trial is on, where tfw-boot runs it (ota_trial.h): the platform
 * block's boot_state is TRIAL. No valid block is no trial.
 */

#include <stdint.h>

#include "tfw/boot.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "boot_platform.h"
#include "ota_trial.h"

uint8_t tfw_ota_trial_on(void)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;

    return (uint8_t)(tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN) == TFW_OK
                     && tfw_boot_platform_parse(raw, &plat) == TFW_OK
                     && plat.boot_state == TFW_BOOT_STATE_TRIAL);
}
