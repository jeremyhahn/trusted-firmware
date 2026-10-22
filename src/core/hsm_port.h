/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response engine's one way out (docs/hsm-self-defence/
 * architecture.md section 3, "Where the code sits"). Implemented once per SoC
 * (the M7's SCMI and enclave clients, slices 5 and 6) and by a test's mock,
 * chosen by the object list and linked, never through a vtable.
 */
#ifndef TFW_CORE_HSM_PORT_H
#define TFW_CORE_HSM_PORT_H

#include <stdint.h>

#include "tfw/status.h"

/* Perform action A1-A6 (hsm.h, TFW_HSM_ACT_*). TFW_OK once it is done; for
 * keys-wipe, only once the enclave's strict operation is acknowledged (A4).
 * An action the SoC cannot perform yet returns TFW_ERR_UNSUPPORTED, and the
 * engine carries on with the next. */
tfw_status_t tfw_hsm_port_act(uint8_t action);

/* The tenant's public key every policy-set and script envelope is verified
 * against (ADR D3): TFW_SUIT_KEY_LEN bytes, read-only in the image. Never
 * taken from the channel, so the A55 cannot name the key its own policy is
 * checked under. */
const uint8_t *tfw_hsm_port_key(void);

#endif
