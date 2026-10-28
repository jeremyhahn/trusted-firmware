/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 port's signal intake (#50 slice 10, hsm_intake.h): S1 from the
 * System Manager's notification, S5 from the pin interrupt, S2 polled from
 * the enclave.
 */

#include "hsm_intake.h"

#include <stddef.h>

#include "core/hsm.h"
#include "scmi.h"

#define SIG_BIT(sig) ((uint8_t)(1u << ((sig) - 1u)))

void tfw_imx95_hsm_notified(struct tfw_imx95_hsm_intake *in,
                            const struct tfw_imx95_hsm_intake_ids *ids, uint32_t hdr)
{
    if (ids->s1_proto == 0u || TFW_SCMI_HDR_TYPE(hdr) != TFW_SCMI_TYPE_NOTIFICATION ||
        TFW_SCMI_HDR_PROTO(hdr) != ids->s1_proto || TFW_SCMI_HDR_MSG_ID(hdr) != ids->s1_msg) {
        return;
    }
    in->s1_seen = in->s1_seen + 1u;
}

void tfw_imx95_hsm_pin(struct tfw_imx95_hsm_intake *in)
{
    in->s5_seen = in->s5_seen + 1u;
}

uint8_t tfw_imx95_hsm_intake_sense(struct tfw_imx95_hsm_intake *in)
{
    uint32_t s1 = in->s1_seen;
    uint32_t s5 = in->s5_seen;
    uint8_t  s  = 0u;

    if (s1 != in->s1_last) {
        in->s1_last = s1;
        s |= SIG_BIT(TFW_HSM_SIG_TAMPER_PIN);
    }
    if (s5 != in->s5_last) {
        in->s5_last = s5;
        s |= SIG_BIT(TFW_HSM_SIG_HOTPLUG);
    }
    return s;
}

uint8_t tfw_imx95_hsm_poll(struct tfw_imx95_hsm_intake *in, const struct tfw_ele_chan *c,
                           const struct tfw_ele_event_layout *l,
                           const struct tfw_imx95_hsm_intake_ids *ids, uint8_t mu_irq)
{
    uint32_t ev[TFW_ELE_EVENTS_MAX];
    uint32_t n = 0u, i;
    int32_t  rc;

    if (c->mu == 0u || l->command == 0u || ids->s2_event == 0u) {
        return 0u;
    }
    if (in->session == 0u && tfw_ele_session_open(c, mu_irq, &in->session, NULL) != 0) {
        return 0u;
    }
    rc = tfw_ele_get_event(c, l, in->session, ev, TFW_ELE_EVENTS_MAX, &n, NULL);
    if (rc == TFW_ELE_E_SESSION) {
        in->session = 0u;
    }
    if (rc != 0) {
        return 0u;
    }
    for (i = 0u; i < n; i++) {
        if (ev[i] == ids->s2_event) {
            return SIG_BIT(TFW_HSM_SIG_AHAB_FAILURE);
        }
    }
    return 0u;
}
