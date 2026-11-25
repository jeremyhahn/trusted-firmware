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

uint32_t tfw_scmi_crc32(uint32_t seed, const uint8_t *p, uint32_t n)
{
    uint32_t crc = ~seed, i, b;

    for (i = 0u; i < n; i++) {
        crc ^= p[i];
        for (b = 0u; b < 8u; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* The CRC imx-sm keeps at TFW_SCMI_SHM_CRC: over the header word and the
 * payload, as they lie in the area (little-endian). */
static uint32_t msg_crc(const struct tfw_scmi_msg *m)
{
    uint8_t h[4];

    h[0] = (uint8_t)m->header;
    h[1] = (uint8_t)(m->header >> 8);
    h[2] = (uint8_t)(m->header >> 16);
    h[3] = (uint8_t)(m->header >> 24);
    return tfw_scmi_crc32(tfw_scmi_crc32(0u, h, 4u), m->payload, m->len);
}

int32_t tfw_scmi_xfer(const struct tfw_scmi_chan *c, const struct tfw_scmi_msg *cmd,
                      struct tfw_scmi_msg *rsp)
{
    uint32_t room, len, i, w = 0u;

    if (c->shm_len < TFW_SCMI_SHM_PAYLOAD) {
        return TFW_SCMI_E_TOO_LONG;   /* no room for flags, length and header */
    }
    room = (c->shm_len - TFW_SCMI_SHM_PAYLOAD) & ~3u;   /* whole words only */
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
    if (c->crc != 0u) {
        SHM(c, TFW_SCMI_SHM_CRC) = msg_crc(cmd);
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
    if (c->crc != 0u && SHM(c, TFW_SCMI_SHM_CRC) != msg_crc(rsp)) {
        return TFW_SCMI_E_CRC;
    }
    return tfw_scmi_check(cmd, rsp);
}
