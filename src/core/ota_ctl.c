/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * OTA_INFO, OTA_ACTIVATE and OTA_CONFIRM -- issue #77, ADR decision 8 (owner
 * ruling 2026-10-17, items 21 and 55), docs/ota/adr-ota-image-format-and-trust.md.
 *
 *   OTA_INFO     [slot:1] -> [slots:1][active:1][flags:1][floor:4][state:1]
 *                            [image_version:4][measurement:32]
 *                Read only. Slot 0 is the running image, its state the platform
 *                block's boot_state; slot 1 is the store, its state PENDING when
 *                the MARK names a whole image (version and measurement from its
 *                trailer), the block's ota_state otherwise. Slot 0's version and
 *                measurement are zero: the running image's are attestation's.
 *                flags bit 0: ota_state FAILED (the last boot fell back); bit 2:
 *                TRIAL. Bit 1 (refused at evaluation) is not told apart here.
 *   OTA_CONFIRM  In TRIAL only: the confirm request record (include/tfw/boot.h)
 *                bound to the running image, and the trial's deadline stopped.
 *                The platform block is not written; tfw-boot takes the request
 *                at the next reset, settles the trial and advances the floor.
 *   OTA_ACTIVATE [mode:1] -- 0 at the next reset, 1 reset now. The image OTA_END
 *                verified is read back and hashed again, and only then is the
 *                store's MARK written (VERIFIED -> PENDING); an image that cannot
 *                be verified fails closed: TFW_ERR_BAD_STATE and nothing is
 *                written. Refused in TRIAL and CONFIRMED, which the next reset
 *                handles first.
 *
 * The store's MARK is the record tfw-boot takes at reset (ota_stage.c); only
 * ACTIVATE writes it, so an image ended and not activated is never taken.
 * CONFIRM and ACTIVATE take the operator auth session where the tier has one.
 * No static state: the block is read into the stack on each call.
 */
#include <string.h>
#include "tfw/boot.h"
#include "tfw/config.h"
#include "tfw/hal.h"
#include "boot_platform.h"
#include "boot_trial.h"
#include "auth_session.h"
#include "ota_stage.h"
#include "ota_trial.h"
#include "ota_ctl.h"
#include "ota_floor.h"
#include "handoff.h"
#include "crc16.h"
#include "../caps/sys.h"

#define INFO_FLAG_FELL_BACK 0x01u
#define INFO_FLAG_TRIAL     0x04u

static tfw_status_t load(uint8_t raw[TFW_BOOT_EE_LEN], tfw_boot_platform_t *p)
{
    tfw_status_t st = tfw_hal_nvm_read(TFW_BOOT_EE_BASE, raw, TFW_BOOT_EE_LEN);

    return (st == TFW_OK) ? tfw_boot_platform_parse(raw, p) : st;
}

tfw_status_t tfw_otactl_cmd_info(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;
    uint8_t             slot = tfw_read_u8(r);
    uint8_t             flags = 0u;
    uint8_t             state;
    uint32_t            version = 0u;
    uint32_t            rollback = 0u;
    tfw_status_t        st;

    if (slot > 1u) {
        return TFW_ERR_RANGE;
    }
    st = load(raw, &plat);
    if (st == TFW_OK) {
        st = tfw_otactl_floor(&plat, &rollback);   /* the element's, where there is one */
    }
    if (st != TFW_OK) {
        return st;
    }
    if (plat.ota_state == TFW_BOOT_OTA_FAILED) {
        flags |= INFO_FLAG_FELL_BACK;
    }
    if (plat.boot_state == TFW_BOOT_STATE_TRIAL) {
        flags |= INFO_FLAG_TRIAL;
    }
    memset(raw, 0, TFW_BOOT_MEASURE_LEN);   /* raw is spent: the measurement */
    if (slot == 0u) {
        state = plat.boot_state;
    } else if (tfw_stage_marked(raw, &version, 0u) == TFW_OK) {
        state = TFW_BOOT_OTA_PENDING;
    } else {
        memset(raw, 0, TFW_BOOT_MEASURE_LEN);
        state = plat.ota_state;
    }
    tfw_write_u8(w, 2u);
    tfw_write_u8(w, 0u);
    tfw_write_u8(w, flags);
    tfw_write_u32(w, rollback);
    tfw_write_u8(w, state);
    tfw_write_u32(w, version);
    tfw_write_blob(w, raw, TFW_BOOT_MEASURE_LEN);
    return TFW_OK;
}

tfw_status_t tfw_otactl_cmd_confirm(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t plat;
    const uint8_t      *meas = tfw_boot_handoff_measurement();
    uint16_t            crc;
    tfw_status_t        st;

    (void)r;
    (void)w;
    st = tfw_auth_gate();
    if (st == TFW_OK) {
        st = load(raw, &plat);
    }
    if (st == TFW_OK) {
        st = tfw_boot_trial_confirm(&plat);   /* TRIAL only; plat is not stored */
    }
    if (st == TFW_OK && meas == NULL) {
        st = TFW_ERR_BAD_STATE;               /* no handoff: no image to name */
    }
    if (st != TFW_OK) {
        return st;
    }
    /* The request, never the block (include/tfw/hal.h: tfw-boot is its one
     * writer). Bound to the running image by its measurement; a torn write
     * fails its CRC and the trial fails at reset, as an unconfirmed one does. */
    raw[0] = TFW_BOOT_CONFIRM_MAGIC;
    memcpy(&raw[TFW_BOOT_CONFIRM_O_MEAS], meas, TFW_BOOT_CONFIRM_MEAS_LEN);
    crc = tfw_crc16(raw, TFW_BOOT_CONFIRM_O_CRC);
    raw[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)crc;
    raw[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(crc >> 8);
    st = tfw_hal_eeprom_write(TFW_BOOT_CONFIRM_ADDR, raw, TFW_BOOT_CONFIRM_LEN);
    if (st == TFW_OK) {
        tfw_ota_trial_stop();
    }
    return st;
}

