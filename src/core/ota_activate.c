/*
 * OTA_ACTIVATE (OTA ADR decision 8, issue #77): the control plane's call to
 * take the staged image. Every stager links this file, a single-slot board
 * (the 328P, the 32U4) as well as a BOOT_AB one, because OTA_END no longer
 * writes the MARK: without it a single-slot stager could stage an image and
 * never install it. mk/firmware.mk links it with every OTA_STAGE=1 image and
 * sets TFW_FEATURE_OTA_ACTIVATE; OTA_INFO and OTA_CONFIRM stay in ota_ctl.c,
 * linked only where tfw-boot runs the trial.
 *
 * The stager re-verifies the store and takes only the image OTA_END verified
 * (tfw_stage_activate). A board in a trial, or one that has confirmed its
 * image, has nothing to take: TFW_ERR_BAD_STATE.
 */

#include "tfw/boot.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "boot_platform.h"
#include "auth_session.h"
#include "ota_stage.h"
#include "ota_ctl.h"
#include "../caps/sys.h"

static tfw_status_t load(uint8_t raw[TFW_BOOT_EE_LEN], tfw_boot_platform_t *p)
{
    tfw_status_t st = tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN);

    return (st == TFW_OK) ? tfw_boot_platform_parse(raw, p) : st;
}

tfw_status_t tfw_otactl_cmd_activate(tfw_reader_t *r, tfw_writer_t *w)
{
    tfw_boot_platform_t plat;
    uint8_t             mode = tfw_read_u8(r);
    tfw_status_t        st;

    (void)w;
    if (mode > 1u) {
        return TFW_ERR_RANGE;
    }
    st = tfw_auth_gate();
    if (st == TFW_OK) {
        uint8_t raw[TFW_BOOT_EE_LEN];

        st = load(raw, &plat);
    }
    if (st != TFW_OK) {
        return st;
    }
    if (plat.boot_state == TFW_BOOT_STATE_TRIAL
        || plat.boot_state == TFW_BOOT_STATE_CONFIRMED
        || tfw_stage_activate() != TFW_OK) {
        return TFW_ERR_BAD_STATE;
    }
    if (mode == 1u) {
        tfw_sys_reset_soon();
    }
    return TFW_OK;
}
