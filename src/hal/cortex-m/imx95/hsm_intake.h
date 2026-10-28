/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's signal intake (#50 slice 10): how S1, S2 and S5 reach
 * tfw_hsm_port_sense, on a given state and ids so a host test can inject each
 * event with ids the board does not have yet (board_profile.h, zero until a
 * source names them).
 *
 * S1, tamper: an SCMI notification from the System Manager on the M7 agent's
 * channel (architecture question 2 (b)). Its protocol and message ids are open
 * (IMX95SRM), and so are the channel's notification receiver and the
 * protocol's NOTIFY subscription: the receiver, when it exists, calls
 * tfw_imx95_hsm_notified with each notification's header.
 * S2, AHAB failure: hsm_get_event polled on every pass (question 6 (a),
 * unconfirmed until the slice 10 bench test).
 * S5, hotplug: an M7 GPIO pin interrupt, no fuse burnt (question 1 (a)); the
 * pad is open (board schematic) and is the one the System Manager assigns
 * (question 11 (c), the A55's channel report a hint beside it).
 *
 * Each interrupt bumps a counter its handler alone writes; the loop alone
 * writes what it last saw, so no interrupt lock is needed. A counter that
 * moved since the last pass is one rising edge: the next pass reads it low.
 */
#ifndef TFW_CM_IMX95_HSM_INTAKE_H
#define TFW_CM_IMX95_HSM_INTAKE_H

#include <stdint.h>

#include "ele_mu.h"

/* The ids each event is matched against, zero meaning unknown: an event
 * whose id is unknown is never taken, and nothing is sent to look for it. */
struct tfw_imx95_hsm_intake_ids {
    uint32_t s1_proto;  /* S1's SCMI protocol id (IMX95SRM) */
    uint32_t s1_msg;    /* S1's notification message id (IMX95SRM) */
    uint32_t s2_event;  /* S2's enclave event code (IMX95RM, imx-secure-enclave) */
};

struct tfw_imx95_hsm_intake {
    volatile uint32_t s1_seen;  /* written by the notification's handler */
    volatile uint32_t s5_seen;  /* written by the pin's handler */
    uint32_t s1_last;           /* written by the loop */
    uint32_t s5_last;
    uint32_t session;           /* the enclave session S2 polls on; 0 none */
};

/* Take one SCMI message header from the channel's receiver: S1 counts when it
 * is a notification of protocol `s1_proto`, nonzero, and message `s1_msg`. */
void tfw_imx95_hsm_notified(struct tfw_imx95_hsm_intake *in,
                            const struct tfw_imx95_hsm_intake_ids *ids, uint32_t hdr);

/* Take one S5 pin interrupt. */
void tfw_imx95_hsm_pin(struct tfw_imx95_hsm_intake *in);

/* The S1 and S5 bits (hsm_port.h, tfw_hsm_port_sense) of every counter that
 * moved since the last call. */
uint8_t tfw_imx95_hsm_intake_sense(struct tfw_imx95_hsm_intake *in);

/* Poll the enclave's events for S2 and return its bit if one equals
 * `s2_event`. Nothing is sent while the MU, the message id or `s2_event` is
 * 0. The session is opened on the first poll with `mu_irq` and kept; one the
 * enclave no longer knows is dropped and opened again on the next pass. */
uint8_t tfw_imx95_hsm_poll(struct tfw_imx95_hsm_intake *in, const struct tfw_ele_chan *c,
                           const struct tfw_ele_event_layout *l,
                           const struct tfw_imx95_hsm_intake_ids *ids, uint8_t mu_irq);

/* The port's interrupt entries on its own state and the board's ids
 * (hsm_port.c): S1's notification receiver calls the first with each
 * header, S5's pin handler the second. */
void tfw_imx95_hsm_notify_isr(uint32_t hdr);
void tfw_imx95_hsm_pin_isr(void);

#endif /* TFW_CM_IMX95_HSM_INTAKE_H */
