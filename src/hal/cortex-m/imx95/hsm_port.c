/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response engine's port on the i.MX 95 M7 (src/core/hsm_port.h;
 * docs/hsm-self-defence/m7-design.md section 7, slice 5): A1, A2 and A5 as SCMI
 * messages to the System Manager (Arm DEN0056E), over the MU channel.
 *
 *   A1 PHYs off  RESET held asserted on the PHYs' reset domain, then
 *                POWER_STATE_SET off on their power domain, each where its id
 *                is set: which of the two holds the PHYs is architecture Q10
 *   A2 A55 off   POWER_STATE_SET off on the A55's power domain
 *   A5 shutdown  SYSTEM_POWER_STATE_SET, a forceful shutdown of the system,
 *                once the board says the M7's agent may send it (Q7)
 *   A4 wipe      each key deleted by the EdgeLock enclave as a strict
 *                operation, done only once every delete is acknowledged
 *                (slice 6); keys only, the key-group delete is question 5
 *   S3 measure   GET_INFO from the enclave, compared with the policy set's
 *                expected values (tfw_hsm_port_measure, slice 6)
 *
 * A3 stays unsupported: question 4 says the enclave binds a key store to the
 * domain and MU that opened it, so the M7 likely cannot lock the A55's. The
 * enclave's MU, ids and layouts are board_profile.h's placeholders, all zero.
 *
 * The other actions answer TFW_ERR_UNSUPPORTED until their slices, and the
 * engine carries on with the next. The channel and the ids are the board's
 * (board_profile.h), all zero until docs/multi-arch/imx95-design.md question
 * 21 names them, so on the board today every action is unsupported and nothing
 * is sent. A reply's status maps as scmi.c's tfw_scmi_system_power_status maps
 * it: NOT_FOUND, which these commands list and that one does not, reads as
 * unrecognised, TFW_ERR_INTERNAL.
 *
 * The tenant key, tfw_hsm_port_key, is not here: only the channel's commands
 * (hsm_cmd.c) take it, and which verifier the M7 checks a policy with is open
 * (m7-design.md question O2).
 */

#include <string.h>

#include "core/hsm.h"
#include "core/hsm_port.h"
#include "hsm_act.h"
#include "scmi.h"
#include "tfw/config.h"

static tfw_status_t send(const struct tfw_scmi_chan *c, const struct tfw_scmi_msg *cmd)
{
    struct tfw_scmi_msg rsp;

    return tfw_scmi_system_power_status(tfw_scmi_xfer(c, cmd, &rsp));
}

static tfw_status_t power_off(struct tfw_scmi_chan *c, uint32_t domain)
{
    struct tfw_scmi_msg cmd;

    tfw_scmi_power_state_set(&cmd, tfw_scmi_token(c), domain, TFW_SCMI_POWER_OFF);
    return send(c, &cmd);
}

static tfw_status_t reset_hold(struct tfw_scmi_chan *c, uint32_t domain)
{
    struct tfw_scmi_msg cmd;

    tfw_scmi_reset(&cmd, tfw_scmi_token(c), domain, TFW_SCMI_RESET_ASSERT,
                   TFW_SCMI_RESET_COLD);
    return send(c, &cmd);
}

static tfw_status_t phys_off(struct tfw_scmi_chan *c, const struct tfw_imx95_hsm_ids *ids)
{
    tfw_status_t st = TFW_OK;

    if (ids->phy_reset != 0u) {
        st = reset_hold(c, ids->phy_reset);
    }
    if (ids->phy_power != 0u) {
        tfw_status_t s2 = power_off(c, ids->phy_power);
        if (st == TFW_OK) {
            st = s2;
        }
    }
    return st;
}

/* A5: SYSTEM_POWER_STATE_SET, shutdown. Forceful (flag bit 0 clear, scmi.h),
 * not graceful: a graceful one would wait on the A55's software, which A5 runs
 * because it is not trusted (architecture.md section 3, A5). */
static tfw_status_t shutdown(struct tfw_scmi_chan *c)
{
    struct tfw_scmi_msg cmd;

    tfw_scmi_system_power_state_set(&cmd, tfw_scmi_token(c), TFW_SCMI_SYSTEM_FORCEFUL,
                                    TFW_SCMI_SYSTEM_SHUTDOWN);
    return send(c, &cmd);
}

tfw_status_t tfw_imx95_hsm_act(struct tfw_scmi_chan *c, const struct tfw_imx95_hsm_ids *ids,
                               uint8_t action)
{
    if (c->mu == 0u || c->shm == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    switch (action) {
    case TFW_HSM_ACT_PHY_OFF:
        if (ids->phy_reset == 0u && ids->phy_power == 0u) {
            return TFW_ERR_UNSUPPORTED;
        }
        return phys_off(c, ids);
    case TFW_HSM_ACT_POWER_OFF:
        if (ids->a55_power == 0u) {
            return TFW_ERR_UNSUPPORTED;
        }
        return power_off(c, ids->a55_power);
    case TFW_HSM_ACT_SHUTDOWN:
        if (ids->shutdown == 0u) {
            return TFW_ERR_UNSUPPORTED;
        }
        return shutdown(c);
    default:
        return TFW_ERR_UNSUPPORTED;
    }
}

/* The enclave client's result as the engine reads it. */
static tfw_status_t ele_status(int32_t rc)
{
    if (rc == 0) {
        return TFW_OK;
    }
    if (rc == TFW_ELE_E_TIMEOUT) {
        return TFW_ERR_TIMEOUT;
    }
    if (rc == TFW_ELE_E_LAYOUT) {
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_ERR_CRYPTO;
}

/* Delete each key on the open service `mgmt`; the first failure, or 0. */
static int32_t delete_keys(const struct tfw_ele_chan *c, const struct tfw_imx95_wipe *w,
                           uint32_t mgmt)
{
    int32_t first = 0;
    uint32_t i;

    for (i = 0u; i < TFW_IMX95_WIPE_KEYS && w->keys[i] != 0u; i++) {
        int32_t rc = tfw_ele_key_delete(c, &w->del, mgmt, w->keys[i], NULL);
        if (first == 0) {
            first = rc;
        }
    }
    return first;
}

tfw_status_t tfw_imx95_hsm_wipe(const struct tfw_ele_chan *c, const struct tfw_imx95_wipe *w)
{
    uint32_t session = 0u, store = 0u, mgmt = 0u;
    int32_t  rc;

    if (c->mu == 0u || w->del.command == 0u || w->keys[0] == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    rc = tfw_ele_session_open(c, w->mu_irq, &session, NULL);
    if (rc != 0) {
        return ele_status(rc);
    }
    rc = tfw_ele_key_store_open(c, session, w->store_id, w->store_nonce,
                                TFW_ELE_KEY_STORE_LOAD, &store, NULL);
    if (rc == 0) {
        rc = tfw_ele_key_mgmt_open(c, store, &mgmt, NULL);
        if (rc == 0) {
            rc = delete_keys(c, w, mgmt);
            /* A close the enclave refuses undoes no acknowledged delete:
             * each was committed to NVM before its answer. */
            (void)tfw_ele_key_mgmt_close(c, mgmt, NULL);
        }
        (void)tfw_ele_key_store_close(c, store, NULL);
    }
    (void)tfw_ele_session_close(c, session, NULL);
    return ele_status(rc);
}

tfw_status_t tfw_imx95_hsm_compare(const uint8_t info[TFW_ELE_GET_INFO_LEN],
                                   const tfw_hsm_set_t *set, const struct tfw_imx95_s3 *s3)
{
    uint8_t i;

    for (i = 0u; i < set->n_expected; i++) {
        const tfw_hsm_expected_t        *e = &set->expected[i];
        const struct tfw_imx95_s3_field *f = NULL;
        uint32_t k;

        for (k = 0u; k < TFW_IMX95_S3_FIELDS; k++) {
            if (s3->f[k].key != 0u && s3->f[k].key == e->key) {
                f = &s3->f[k];
            }
        }
        if (f == NULL || f->len == 0u || (uint32_t)f->off + f->len > TFW_ELE_GET_INFO_LEN
            || e->n != f->len || memcmp(&info[f->off], e->value, f->len) != 0) {
            return TFW_ERR_RANGE;
        }
    }
    return TFW_OK;
}

tfw_status_t tfw_imx95_hsm_measure(const struct tfw_ele_chan *c, uint8_t info[TFW_ELE_GET_INFO_LEN],
                                   uint64_t info_addr, const tfw_hsm_set_t *set,
                                   const struct tfw_imx95_s3 *s3)
{
    int32_t rc;

    if (c->mu == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    memset(info, 0, TFW_ELE_GET_INFO_LEN);
    rc = tfw_ele_get_info(c, info_addr, NULL);
    if (rc != 0) {
        return ele_status(rc);
    }
    return tfw_imx95_hsm_compare(info, set, s3);
}

static struct tfw_scmi_chan g_chan = {
    TFW_IMX95_SCMI_MU_BASE, TFW_IMX95_SCMI_SHM_BASE, TFW_IMX95_SCMI_SHM_LEN,
    TFW_IMX95_SCMI_DOORBELL, 0u
};

static const struct tfw_imx95_hsm_ids g_ids = {
    TFW_IMX95_HSM_PHY_RESET, TFW_IMX95_HSM_PHY_POWER, TFW_IMX95_HSM_A55_POWER,
    TFW_IMX95_HSM_SHUTDOWN
};

static const struct tfw_ele_chan g_ele = { TFW_IMX95_ELE_MU_BASE };

static const struct tfw_imx95_wipe g_wipe = {
    TFW_IMX95_ELE_MU_IRQ, TFW_IMX95_HSM_KEY_STORE, TFW_IMX95_HSM_KEY_NONCE,
    { TFW_IMX95_HSM_WIPE_KEYS },
    { TFW_IMX95_ELE_DEL_CMD, TFW_IMX95_ELE_DEL_CMD_WORDS, TFW_IMX95_ELE_DEL_RSP_WORDS,
      TFW_IMX95_ELE_DEL_W_HANDLE, TFW_IMX95_ELE_DEL_W_KEY, TFW_IMX95_ELE_DEL_W_FLAGS,
      TFW_IMX95_ELE_DEL_FLAGS_SHIFT }
};

static const struct tfw_imx95_s3 g_s3 = { {
    { TFW_IMX95_S3_KEY_LMDA, TFW_ELE_INFO_LMDA_OFF, TFW_ELE_INFO_LMDA_LEN },
    { TFW_IMX95_S3_KEY_ROM_PATCH, TFW_IMX95_ELE_INFO_ROM_PATCH_OFF,
      TFW_IMX95_ELE_INFO_ROM_PATCH_LEN },
    { TFW_IMX95_S3_KEY_SHA_FW, TFW_ELE_INFO_SHA_FW_OFF, TFW_ELE_INFO_SHA_FW_LEN },
    { TFW_IMX95_S3_KEY_OEM_SRKH, TFW_IMX95_ELE_INFO_OEM_SRKH_OFF,
      TFW_IMX95_ELE_INFO_OEM_SRKH_LEN },
    { TFW_IMX95_S3_KEY_SRK_REVOKE, TFW_IMX95_ELE_INFO_SRK_REVOKE_OFF,
      TFW_IMX95_ELE_INFO_SRK_REVOKE_LEN },
} };

/* GET_INFO's buffer, written by the enclave. Its address as the enclave sees
 * it is taken to be the M7's: to confirm with the MU (question 3). */
static uint8_t g_info[TFW_ELE_GET_INFO_LEN];

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    if (action == TFW_HSM_ACT_KEYS_WIPE) {
        return tfw_imx95_hsm_wipe(&g_ele, &g_wipe);
    }
    return tfw_imx95_hsm_act(&g_chan, &g_ids, action);
}

tfw_status_t tfw_hsm_port_measure(const tfw_hsm_set_t *set)
{
    return tfw_imx95_hsm_measure(&g_ele, g_info, (uint64_t)(uintptr_t)g_info, set, &g_s3);
}
