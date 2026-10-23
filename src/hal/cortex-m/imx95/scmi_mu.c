/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SCMI shared memory channel over an MU doorbell (scmi_mu.h). The order
 * is DEN0056E's (section 5.1.1): the agent writes the message while the
 * channel is free, marks it busy, rings; the platform answers in the same
 * area and marks it free.
 */
#include <stddef.h>
#include <stdint.h>

#include "cm_regs.h"
#include "scmi_mu.h"

#define SHM(c, off) TFW_CM_REG((c)->shm + (off))

static int wait_free(const struct tfw_scmi_chan *c)
{
    uint32_t i;

    for (i = 0u; i < TFW_IMX95_SCMI_POLLS; i++) {
        if ((SHM(c, TFW_SCMI_SHM_STATUS) & TFW_SCMI_SHM_FREE) != 0u) {
            return 1;
        }
    }
    return 0;
}

uint32_t tfw_scmi_token(struct tfw_scmi_chan *c)
{
    uint32_t t = c->token & TFW_SCMI_TOKEN_MASK;

    c->token = (t + 1u) & TFW_SCMI_TOKEN_MASK;
    return t;
}

int32_t tfw_scmi_xfer(const struct tfw_scmi_chan *c, const struct tfw_scmi_msg *cmd,
                      struct tfw_scmi_msg *rsp)
{
    uint32_t room, len, i, w = 0u;

    room = c->shm_len > TFW_SCMI_SHM_PAYLOAD ? c->shm_len - TFW_SCMI_SHM_PAYLOAD : 0u;
    if (cmd->len > room || cmd->len > TFW_SCMI_PAYLOAD_MAX || (cmd->len & 3u) != 0u) {
        return TFW_SCMI_E_TOO_LONG;
    }
    if (!wait_free(c)) {
        return TFW_SCMI_E_TIMEOUT;
    }

    SHM(c, TFW_SCMI_SHM_FLAGS)  = 0u;   /* polled: no completion interrupt */
    SHM(c, TFW_SCMI_SHM_LENGTH) = 4u + cmd->len;
    SHM(c, TFW_SCMI_SHM_HEADER) = cmd->header;
    for (i = 0u; i < cmd->len; i += 4u) {
        SHM(c, TFW_SCMI_SHM_PAYLOAD + i) =
            (uint32_t)cmd->payload[i] | ((uint32_t)cmd->payload[i + 1u] << 8) |
            ((uint32_t)cmd->payload[i + 2u] << 16) | ((uint32_t)cmd->payload[i + 3u] << 24);
    }
    SHM(c, TFW_SCMI_SHM_STATUS) = 0u;   /* busy, no error: the platform's now */
    TFW_CM_REG(c->mu + TFW_IMX95_MU_GCR) |= 1u << c->doorbell;

    if (!wait_free(c)) {
        return TFW_SCMI_E_TIMEOUT;
    }
    if ((SHM(c, TFW_SCMI_SHM_STATUS) & TFW_SCMI_SHM_ERROR) != 0u) {
        return TFW_SCMI_E_CHANNEL;
    }
    len = SHM(c, TFW_SCMI_SHM_LENGTH);
    if (len < 4u) {
        return TFW_SCMI_E_SHORT;
    }
    len -= 4u;
    if (len > room || len > TFW_SCMI_PAYLOAD_MAX) {
        return TFW_SCMI_E_TOO_LONG;
    }
    rsp->header = SHM(c, TFW_SCMI_SHM_HEADER);
    rsp->len    = len;
    for (i = 0u; i < len; i++) {
        if ((i & 3u) == 0u) {
            w = SHM(c, TFW_SCMI_SHM_PAYLOAD + i);
        }
        rsp->payload[i] = (uint8_t)((w >> (8u * (i & 3u))) & 0xFFu);
    }
    return tfw_scmi_check(cmd, rsp);
}
