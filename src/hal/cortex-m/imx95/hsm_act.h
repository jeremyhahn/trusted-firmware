/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's actions on a given channel and ids (hsm_port.c), so a
 * host test can drive them with ids the board does not have yet: the board's
 * are zero until docs/multi-arch/imx95-design.md question 21 names them, and
 * the enclave's (A4, S3) until a source does (board_profile.h).
 */
#ifndef TFW_CM_IMX95_HSM_ACT_H
#define TFW_CM_IMX95_HSM_ACT_H

#include <stdint.h>

#include "core/hsm.h"
#include "ele_mu.h"
#include "scmi_mu.h"
#include "tfw/status.h"

/* The domains A1 and A2 switch, and whether A5 may be sent, zero meaning
 * unsupported. */
struct tfw_imx95_hsm_ids {
    uint32_t phy_reset;   /* A1: RESET, held asserted (DEN0056E section 4.8.2.6) */
    uint32_t phy_power;   /* A1: POWER_STATE_SET off (section 4.3.2.6) */
    uint32_t a55_power;   /* A2: POWER_STATE_SET off */
    uint32_t shutdown;    /* A5: SYSTEM_POWER_STATE_SET (section 4.4.2.5) */
};

/* Perform `action` (hsm.h, TFW_HSM_ACT_*) on channel `c`. A1 asserts the
 * PHYs' reset and then switches their power domain off, each where its id is
 * set, and tries the second when the first fails; A2 switches the A55's power
 * domain off; A5 asks the System Manager for a forceful shutdown of the
 * system. TFW_OK once every message sent is acknowledged, else the first
 * failure as tfw_scmi_system_power_status maps it. TFW_ERR_UNSUPPORTED, with
 * nothing sent, for every other action, for an action whose ids are all zero,
 * and while the channel's MU or shared memory is zero. */
tfw_status_t tfw_imx95_hsm_act(struct tfw_scmi_chan *c, const struct tfw_imx95_hsm_ids *ids,
                               uint8_t action);

/* A4, keys only: the enclave key store and keys to delete, and the delete's
 * layout. Zero key ids are unused. The board's are placeholders
 * (board_profile.h). */
#define TFW_IMX95_WIPE_KEYS 4u

struct tfw_imx95_wipe {
    uint8_t                   mu_irq;       /* SESSION_OPEN's interrupt index */
    uint32_t                  store_id;     /* the key store, loaded */
    uint32_t                  store_nonce;
    uint32_t                  keys[TFW_IMX95_WIPE_KEYS];
    struct tfw_ele_del_layout del;
};

/* Delete every key `w` names, each a strict operation (ele_mu.h,
 * tfw_ele_key_delete), in one session, key store and key management service
 * opened for it and closed after. TFW_OK only once the enclave has
 * acknowledged every delete: anything less is not a wipe, and the engine
 * locks down (R3). A refused open or delete is TFW_ERR_CRYPTO, no answer
 * TFW_ERR_TIMEOUT; the remaining keys are still tried. TFW_ERR_UNSUPPORTED,
 * with nothing sent, while the MU, the delete's id or the first key id is
 * zero, and for a layout the client refuses.
 *
 * A3 is not here: it is open (architecture.md section 3, question 4). The
 * key-group delete is not either (question 5). */
tfw_status_t tfw_imx95_hsm_wipe(const struct tfw_ele_chan *c, const struct tfw_imx95_wipe *w);

/* S3: where each field the device attestation reports sits in GET_INFO's
 * buffer, and the policy set's key for it. Key 0 is no key; length 0 a field
 * the port cannot read. */
#define TFW_IMX95_S3_FIELDS 5u

struct tfw_imx95_s3_field {
    uint32_t key;
    uint16_t off;
    uint16_t len;
};

struct tfw_imx95_s3 {
    struct tfw_imx95_s3_field f[TFW_IMX95_S3_FIELDS];
};

/* Compare GET_INFO's buffer `info` with every expected value `set` carries.
 * TFW_OK when each is, byte for byte and in length, the field its key names.
 * TFW_ERR_RANGE when one is not, when no field has its key, or when that
 * field cannot be read. */
tfw_status_t tfw_imx95_hsm_compare(const uint8_t info[TFW_ELE_GET_INFO_LEN],
                                   const tfw_hsm_set_t *set, const struct tfw_imx95_s3 *s3);

/* Ask the enclave for GET_INFO into `info`, the buffer at `info_addr` as the
 * enclave sees it, cleared first, and compare it (tfw_imx95_hsm_compare).
 * TFW_ERR_UNSUPPORTED, with nothing sent, while the MU is zero. A refused
 * GET_INFO is TFW_ERR_CRYPTO, no answer TFW_ERR_TIMEOUT: S3 either way. */
tfw_status_t tfw_imx95_hsm_measure(const struct tfw_ele_chan *c, uint8_t info[TFW_ELE_GET_INFO_LEN],
                                   uint64_t info_addr, const tfw_hsm_set_t *set,
                                   const struct tfw_imx95_s3 *s3);

/* Compare the `len` bytes of `buf` with every expected value `set` carries,
 * as tfw_imx95_hsm_compare does GET_INFO's: a field past `len` cannot be
 * read, and is TFW_ERR_RANGE. */
tfw_status_t tfw_imx95_hsm_compare_buf(const uint8_t *buf, uint32_t len,
                                       const tfw_hsm_set_t *set, const struct tfw_imx95_s3 *s3);

/* S3 by the enclave's device attestation (m7-design.md slice 9): the
 * message's layout, SESSION_OPEN's interrupt index for the session it is
 * asked on (RM00284 section 3.12.4.1: any open session may call it), and
 * where each field sits in the report and its key. All board_profile.h
 * placeholders until a source lays the message and the report out. */
struct tfw_imx95_attest {
    uint8_t                      mu_irq;
    struct tfw_ele_attest_layout l;
    struct tfw_imx95_s3          s3;
};

/* Open a session, ask the enclave for its device attestation under `nonce`
 * into `report`, `report_len` bytes at `report_addr` as the enclave sees it,
 * cleared first, close the session, and compare the report
 * (tfw_imx95_hsm_compare_buf over the layout's report size).
 * TFW_ERR_UNSUPPORTED, with nothing sent, while the MU or the message's id is
 * zero, or for a layout the client refuses. A refused session or attestation, a nonce or a buffer the client
 * refuses, is TFW_ERR_CRYPTO, no answer TFW_ERR_TIMEOUT: S3 either way. */
tfw_status_t tfw_imx95_hsm_attest(const struct tfw_ele_chan *c, const struct tfw_imx95_attest *a,
                                  uint8_t *report, uint32_t report_len, uint64_t report_addr,
                                  const uint8_t *nonce, uint32_t nonce_len,
                                  const tfw_hsm_set_t *set);

#endif /* TFW_CM_IMX95_HSM_ACT_H */
