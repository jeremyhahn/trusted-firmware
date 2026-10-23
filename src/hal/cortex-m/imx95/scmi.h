/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SCMI messages the i.MX 95 M7 sends NXP's System Manager (SPEC row
 * `imx95`), which owns the M7's clocks, power domains and resets.
 *
 * Arm DEN0056, "Arm System Control and Management Interface", version 3.2
 * (docs/reference/README.md, "Not redistributable"). Every value below is the
 * specification's; nothing is designed here. Each is cited by its section and
 * table in DEN0056E, the revision of version 3.2, read against its text for
 * slice 7 of the row; the README holds the title and URL, not the text.
 *
 * Pure: no register access, so the host tests link this file as the M7 image
 * does (test/unit/test_imx95_scmi.c). The transport is scmi_mu.c.
 */
#ifndef TFW_CM_IMX95_SCMI_H
#define TFW_CM_IMX95_SCMI_H

#include <stdint.h>

/* The message header (section 4.1.2, Table 3): message_id in bits 7 to 0,
 * message_type in 9 to 8, protocol_id in 17 to 10, token in 27 to 18; bits
 * 31 to 28 are reserved and zero. A response carries its command's header. */
#define TFW_SCMI_HDR_MSG_ID(h) ((h) & 0xFFu)
#define TFW_SCMI_HDR_TYPE(h)   (((h) >> 8) & 0x3u)
#define TFW_SCMI_HDR_PROTO(h)  (((h) >> 10) & 0xFFu)
#define TFW_SCMI_HDR_TOKEN(h)  (((h) >> 18) & 0x3FFu)
#define TFW_SCMI_TOKEN_MASK    0x3FFu

#define TFW_SCMI_TYPE_COMMAND       0u
#define TFW_SCMI_TYPE_DELAYED_RESP  2u
#define TFW_SCMI_TYPE_NOTIFICATION  3u

/* Protocol identifiers (section 4.1.2, Table 2). */
#define TFW_SCMI_PROTO_BASE  0x10u
#define TFW_SCMI_PROTO_POWER 0x11u
#define TFW_SCMI_PROTO_CLOCK 0x14u
#define TFW_SCMI_PROTO_RESET 0x16u

/* Message identifiers. PROTOCOL_VERSION and PROTOCOL_ATTRIBUTES are 0 and 1
 * in every protocol (section 4.1.3, Table 4). */
#define TFW_SCMI_PROTOCOL_VERSION    0x0u
#define TFW_SCMI_PROTOCOL_ATTRIBUTES 0x1u
#define TFW_SCMI_POWER_STATE_SET     0x4u   /* section 4.3.2.6 */
#define TFW_SCMI_CLOCK_RATE_GET      0x6u   /* section 4.6.2.8 */
#define TFW_SCMI_CLOCK_CONFIG_SET    0x7u   /* section 4.6.2.9 */
#define TFW_SCMI_RESET               0x4u   /* section 4.8.2.6 */

/* POWER_STATE_SET's power_state: the generic ON and OFF states (section
 * 4.3.1, Table 6: bit 30 the state type, 0 on). */
#define TFW_SCMI_POWER_ON  0x00000000u
#define TFW_SCMI_POWER_OFF 0x40000000u

/* RESET's flags: bit 0 autonomous reset, bit 1 explicit assert, bit 2
 * asynchronous (section 4.8.2.6); reset_state 0 is the architectural cold
 * reset (section 4.8.1, Tables 20 and 21). */
#define TFW_SCMI_RESET_AUTONOMOUS 0x1u
#define TFW_SCMI_RESET_ASSERT     0x2u
#define TFW_SCMI_RESET_ASYNC      0x4u
#define TFW_SCMI_RESET_COLD       0x00000000u

/* Status codes (section 4.1.4, Table 5), as the platform returns them. */
#define TFW_SCMI_SUCCESS            0
#define TFW_SCMI_NOT_SUPPORTED     (-1)
#define TFW_SCMI_INVALID_PARAMETERS (-2)
#define TFW_SCMI_DENIED            (-3)
#define TFW_SCMI_NOT_FOUND         (-4)
#define TFW_SCMI_OUT_OF_RANGE      (-5)
#define TFW_SCMI_BUSY              (-6)
#define TFW_SCMI_COMMS_ERROR       (-7)
#define TFW_SCMI_GENERIC_ERROR     (-8)
#define TFW_SCMI_HARDWARE_ERROR    (-9)
#define TFW_SCMI_PROTOCOL_ERROR    (-10)
#define TFW_SCMI_IN_USE            (-11)

/* The client's own results, positive so they never meet a status the
 * platform returns, all of which are zero or negative. */
#define TFW_SCMI_E_TIMEOUT  1   /* the channel stayed busy past the bound */
#define TFW_SCMI_E_HEADER   2   /* the response is not this command's */
#define TFW_SCMI_E_TOO_LONG 3   /* a message longer than the channel or buffer */
#define TFW_SCMI_E_SHORT    4   /* a response too short for its fields */
#define TFW_SCMI_E_CHANNEL  5   /* the platform flagged a channel error */

/* The largest payload either way, in bytes: RATE_GET's 12 is the longest
 * these commands and their responses carry. */
#define TFW_SCMI_PAYLOAD_MAX 32u

struct tfw_scmi_msg {
    uint32_t header;
    uint32_t len;                            /* payload bytes */
    uint8_t  payload[TFW_SCMI_PAYLOAD_MAX];  /* little-endian words */
};

uint32_t tfw_scmi_header(uint32_t proto, uint32_t msg_id, uint32_t type, uint32_t token);

/* The commands. Each overwrites `m` with the command's header and payload. */
void tfw_scmi_protocol_version(struct tfw_scmi_msg *m, uint32_t proto, uint32_t token);
void tfw_scmi_protocol_attributes(struct tfw_scmi_msg *m, uint32_t proto, uint32_t token);
/* `clock_version` is the clock protocol's PROTOCOL_VERSION (0x30000 in
 * DEN0056E, section 4.6.2.1): from major 3 the command carries a third word,
 * extended_config_val, and attributes' state field is bits 1 to 0 (section
 * 4.6.2.9, Table 16); before, bit 0 alone, which is the earlier revisions'
 * layout and not checked here, as their text is not held. Either way 1
 * enables and 0 disables. */
void tfw_scmi_clock_config_set(struct tfw_scmi_msg *m, uint32_t token, uint32_t clock_version,
                               uint32_t clock_id, uint32_t enable);
void tfw_scmi_clock_rate_get(struct tfw_scmi_msg *m, uint32_t token, uint32_t clock_id);
/* Synchronous: flags 0. */
void tfw_scmi_power_state_set(struct tfw_scmi_msg *m, uint32_t token, uint32_t domain_id,
                              uint32_t power_state);
void tfw_scmi_reset(struct tfw_scmi_msg *m, uint32_t token, uint32_t domain_id,
                    uint32_t flags, uint32_t reset_state);

/* The status of `rsp` as the reply to `cmd`: TFW_SCMI_E_HEADER unless its
 * header is the command's, TFW_SCMI_E_SHORT without a status word, else the
 * platform's status, a known one or not. */
int32_t tfw_scmi_check(const struct tfw_scmi_msg *cmd, const struct tfw_scmi_msg *rsp);

/* Payload word `idx` of a response (0 is the status), or TFW_SCMI_E_SHORT. */
int32_t tfw_scmi_word(const struct tfw_scmi_msg *rsp, uint32_t idx, uint32_t *out);

/* PROTOCOL_ATTRIBUTES of the Base protocol (section 4.2.2.3): bits 7 to 0
 * the number of protocols, 15 to 8 the number of agents. */
int32_t tfw_scmi_base_attributes(const struct tfw_scmi_msg *rsp, uint32_t *protocols,
                                 uint32_t *agents);

/* CLOCK_RATE_GET's rate, its low word first (section 4.6.2.8). */
int32_t tfw_scmi_clock_rate(const struct tfw_scmi_msg *rsp, uint64_t *hz);

/* 1 for a status this version of the specification numbers, else 0: a later
 * one reads as unrecognised, never as another. */
int tfw_scmi_status_known(int32_t status);

#endif /* TFW_CM_IMX95_SCMI_H */
