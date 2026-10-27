/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SCMI message encoding and decoding (scmi.h cites Arm DEN0056 for each
 * value). Words are little-endian in the shared memory, as the M7 is.
 */
#include <stddef.h>
#include <stdint.h>

#include "scmi.h"

static void put32(struct tfw_scmi_msg *m, uint32_t v)
{
    uint8_t *p = &m->payload[m->len];

    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    m->len += 4u;
}

static void start(struct tfw_scmi_msg *m, uint32_t proto, uint32_t msg_id, uint32_t token)
{
    m->header = tfw_scmi_header(proto, msg_id, TFW_SCMI_TYPE_COMMAND, token);
    m->len    = 0u;
}

uint32_t tfw_scmi_header(uint32_t proto, uint32_t msg_id, uint32_t type, uint32_t token)
{
    return (msg_id & 0xFFu) | ((type & 0x3u) << 8) | ((proto & 0xFFu) << 10) |
           ((token & TFW_SCMI_TOKEN_MASK) << 18);
}

void tfw_scmi_protocol_version(struct tfw_scmi_msg *m, uint32_t proto, uint32_t token)
{
    start(m, proto, TFW_SCMI_PROTOCOL_VERSION, token);
}

void tfw_scmi_protocol_attributes(struct tfw_scmi_msg *m, uint32_t proto, uint32_t token)
{
    start(m, proto, TFW_SCMI_PROTOCOL_ATTRIBUTES, token);
}

void tfw_scmi_clock_config_set(struct tfw_scmi_msg *m, uint32_t token, uint32_t clock_version,
                               uint32_t clock_id, uint32_t enable)
{
    start(m, TFW_SCMI_PROTO_CLOCK, TFW_SCMI_CLOCK_CONFIG_SET, token);
    put32(m, clock_id);
    put32(m, enable != 0u ? 1u : 0u);
    if ((clock_version >> 16) >= 3u) {
        put32(m, 0u);   /* extended_config_val: no extended configuration */
    }
}

void tfw_scmi_clock_rate_get(struct tfw_scmi_msg *m, uint32_t token, uint32_t clock_id)
{
    start(m, TFW_SCMI_PROTO_CLOCK, TFW_SCMI_CLOCK_RATE_GET, token);
    put32(m, clock_id);
}

void tfw_scmi_power_state_set(struct tfw_scmi_msg *m, uint32_t token, uint32_t domain_id,
                              uint32_t power_state)
{
    start(m, TFW_SCMI_PROTO_POWER, TFW_SCMI_POWER_STATE_SET, token);
    put32(m, 0u);   /* flags: synchronous */
    put32(m, domain_id);
    put32(m, power_state);
}

void tfw_scmi_reset(struct tfw_scmi_msg *m, uint32_t token, uint32_t domain_id,
                    uint32_t flags, uint32_t reset_state)
{
    start(m, TFW_SCMI_PROTO_RESET, TFW_SCMI_RESET, token);
    put32(m, domain_id);
    put32(m, flags);
    put32(m, reset_state);
}

void tfw_scmi_system_power_state_set(struct tfw_scmi_msg *m, uint32_t token, uint32_t flags,
                                     uint32_t system_state)
{
    start(m, TFW_SCMI_PROTO_SYSTEM, TFW_SCMI_SYSTEM_POWER_STATE_SET, token);
    put32(m, flags & TFW_SCMI_SYSTEM_GRACEFUL);   /* bits 31 to 1 reserved, zero */
    put32(m, system_state);
}

tfw_status_t tfw_scmi_lmm_shutdown(struct tfw_scmi_msg *m, uint32_t token, uint32_t lm_id,
                                   uint32_t flags)
{
    if (lm_id == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    start(m, TFW_SCMI_PROTO_LMM, TFW_SCMI_LMM_SHUTDOWN, token);
    put32(m, lm_id);
    put32(m, flags & TFW_SCMI_LMM_GRACEFUL);   /* bits 31 to 1 reserved, zero */
    return TFW_OK;
}

int32_t tfw_scmi_word(const struct tfw_scmi_msg *rsp, uint32_t idx, uint32_t *out)
{
    const uint8_t *p;

    if (idx >= TFW_SCMI_PAYLOAD_MAX / 4u || rsp->len < 4u * (idx + 1u)) {
        return TFW_SCMI_E_SHORT;
    }
    p    = &rsp->payload[4u * idx];
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
    return TFW_SCMI_SUCCESS;
}

int32_t tfw_scmi_check(const struct tfw_scmi_msg *cmd, const struct tfw_scmi_msg *rsp)
{
    uint32_t status;

    if (rsp->header != cmd->header) {
        return TFW_SCMI_E_HEADER;
    }
    if (tfw_scmi_word(rsp, 0u, &status) != TFW_SCMI_SUCCESS) {
        return TFW_SCMI_E_SHORT;
    }
    return (int32_t)status;
}

int32_t tfw_scmi_base_attributes(const struct tfw_scmi_msg *rsp, uint32_t *protocols,
                                 uint32_t *agents)
{
    uint32_t a;

    if (tfw_scmi_word(rsp, 1u, &a) != TFW_SCMI_SUCCESS) {
        return TFW_SCMI_E_SHORT;
    }
    *protocols = a & 0xFFu;
    *agents    = (a >> 8) & 0xFFu;
    return TFW_SCMI_SUCCESS;
}

int32_t tfw_scmi_clock_rate(const struct tfw_scmi_msg *rsp, uint64_t *hz)
{
    uint32_t lo, hi;

    if (tfw_scmi_word(rsp, 1u, &lo) != TFW_SCMI_SUCCESS ||
        tfw_scmi_word(rsp, 2u, &hi) != TFW_SCMI_SUCCESS) {
        return TFW_SCMI_E_SHORT;
    }
    *hz = ((uint64_t)hi << 32) | lo;
    return TFW_SCMI_SUCCESS;
}

int tfw_scmi_status_known(int32_t status)
{
    return status <= TFW_SCMI_SUCCESS && status >= TFW_SCMI_IN_USE;
}

tfw_status_t tfw_scmi_system_power_status(int32_t status)
{
    switch (status) {
    case TFW_SCMI_SUCCESS:            return TFW_OK;
    case TFW_SCMI_INVALID_PARAMETERS: return TFW_ERR_RANGE;
    case TFW_SCMI_NOT_SUPPORTED:      return TFW_ERR_UNSUPPORTED;
    case TFW_SCMI_DENIED:             return TFW_ERR_PROTECTED_REGION;
    case TFW_SCMI_E_TIMEOUT:          return TFW_ERR_TIMEOUT;
    default:                          return TFW_ERR_INTERNAL;
    }
}

tfw_status_t tfw_scmi_lmm_status(int32_t status)
{
    switch (status) {
    case TFW_SCMI_SUCCESS:            return TFW_OK;
    case TFW_SCMI_NOT_FOUND:          return TFW_ERR_UNSUPPORTED;
    case TFW_SCMI_INVALID_PARAMETERS: return TFW_ERR_RANGE;
    case TFW_SCMI_DENIED:             return TFW_ERR_PROTECTED_REGION;
    case TFW_SCMI_E_TIMEOUT:          return TFW_ERR_TIMEOUT;
    default:                          return TFW_ERR_INTERNAL;
    }
}
