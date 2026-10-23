/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SCMI transport from the i.MX 95 M7 to the System Manager: a shared
 * memory channel (Arm DEN0056E, section 5.1, Shared memory based transport) whose
 * doorbell is a general-purpose interrupt of a Messaging Unit. Polled, with a
 * bounded wait: the completion interrupt is never asked for, and nothing
 * sleeps.
 *
 * Which MU, which doorbell bit and which shared memory are the M7 agent's is
 * the System Manager configuration's (trusted-imx95's), and no document this
 * project holds gives them (docs/multi-arch/imx95-design.md question 21). So
 * the channel is the caller's data, and this file names no instance.
 */
#ifndef TFW_CM_IMX95_SCMI_MU_H
#define TFW_CM_IMX95_SCMI_MU_H

#include <stdint.h>

#include "scmi.h"

/* The shared memory area's layout (DEN0056E, section 5.1.2, Table 25):
 * channel_status bit 0 channel free, bit 1 channel error; channel_flags bit 0
 * the completion interrupt (Table 26); length is the header's 4 bytes plus the payload. */
#define TFW_SCMI_SHM_STATUS  0x04u
#define TFW_SCMI_SHM_FLAGS   0x10u
#define TFW_SCMI_SHM_LENGTH  0x14u
#define TFW_SCMI_SHM_HEADER  0x18u
#define TFW_SCMI_SHM_PAYLOAD 0x1Cu
#define TFW_SCMI_SHM_FREE    0x1u
#define TFW_SCMI_SHM_ERROR   0x2u

/* The MU's general-purpose control register, whose bit n raises general
 * interrupt n on the other side, which clears it when taken. Unconfirmed
 * against IMX95RM, the MU chapter, the one primary source, which this project
 * does not hold. Linux v6.12, drivers/mailbox/imx-mailbox.c, agrees, as a
 * secondary source: "fsl,imx95-mu" takes imx_mu_cfg_imx8ulp, an IMX_MU_V2
 * whose control registers are { 0x8, 0x110, 0x114, 0x120, 0x128 }, the third
 * the GCR, with IMX_MU_xCR_GIRn(type, n) BIT(n) on V2. */
#define TFW_IMX95_MU_GCR 0x114u

/* How many times the channel's status is read before the wait gives up, while
 * the channel is busy before a send and after it. A count, not a time: no
 * clock is assumed. */
#define TFW_IMX95_SCMI_POLLS 100000u

struct tfw_scmi_chan {
    uintptr_t mu;        /* the MU's base */
    uintptr_t shm;       /* the shared memory area's base */
    uint32_t  shm_len;   /* its length in bytes */
    uint32_t  doorbell;  /* the GCR bit, 0 to 3 */
    uint32_t  token;     /* the next token */
};

/* The next token, ten bits, wrapping. */
uint32_t tfw_scmi_token(struct tfw_scmi_chan *c);

/* Send `cmd`, wait for the platform's reply, and copy it to `rsp`. Returns
 * tfw_scmi_check's result, or TFW_SCMI_E_TIMEOUT when the channel is busy past
 * TFW_IMX95_SCMI_POLLS before or after the send, TFW_SCMI_E_TOO_LONG when a
 * message either way is longer than the channel or than `rsp`, TFW_SCMI_E_SHORT
 * when the reply has no header, and TFW_SCMI_E_CHANNEL when the platform set
 * channel error. */
int32_t tfw_scmi_xfer(const struct tfw_scmi_chan *c, const struct tfw_scmi_msg *cmd,
                      struct tfw_scmi_msg *rsp);

#endif /* TFW_CM_IMX95_SCMI_MU_H */
