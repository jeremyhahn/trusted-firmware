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
#include "hsm.h"

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

/* The signals the SoC senses itself, read by the loop (hsm_loop.c) on every
 * pass: bit (n - 1) set while signal n's line is asserted (hsm.h,
 * TFW_HSM_SIG_*). The loop raises a signal on its rising edge only, so a line
 * held asserted is one event. A port with no line of its own returns 0. */
uint8_t tfw_hsm_port_sense(void);

/* S3, measurement failure (architecture.md section 2): compare what the SoC's
 * root of trust attests about this boot with the expected values `set`
 * carries (policy-set key 3), called when a set carrying any is loaded.
 * TFW_OK when every one matches. TFW_ERR_UNSUPPORTED when the SoC cannot
 * measure yet: nothing is raised. Anything else is a mismatch, an expected
 * value the port cannot compare, or a measurement that failed, and the
 * engine raises S3. */
tfw_status_t tfw_hsm_port_measure(const tfw_hsm_set_t *set);

#endif
