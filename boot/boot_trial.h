/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one trial: every transition of the OTA state machine, as pure functions.
 *
 * docs/ota/adr-ota-image-format-and-trust.md, decisions 5, 8 and 9, and the
 * machine drawn in docs/ota/architecture.md#device-state-machine. The state is
 * the platform block's boot_state and ota_state (include/tfw/boot.h). These
 * functions read and change only the parsed struct: they touch no NVM, no
 * flash and no register, so every transition and every refusal is reachable
 * from a host test. Writing the result is the caller's, and the caller's
 * comments say what a reset in the middle of that write leaves.
 *
 * A transition that is not valid in the current state answers
 * TFW_ERR_BAD_STATE and changes nothing.
 */
#ifndef TFW_BOOT_TRIAL_H
#define TFW_BOOT_TRIAL_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/* What tfw-boot does with the state it finds at reset, in the ADR's order:
 * TRIAL first (any reset that finds it ends the trial), then CONFIRMED (the
 * floor advances before anything staged is looked at), then PENDING. */
typedef enum {
    TFW_BOOT_RESET_BOOT   = 0, /* nothing in flight: evaluate and boot */
    TFW_BOOT_RESET_FAIL   = 1, /* TRIAL: the trial ended unconfirmed */
    TFW_BOOT_RESET_SETTLE = 2, /* CONFIRMED: floor := version, then GOOD */
    TFW_BOOT_RESET_TRY    = 3  /* PENDING: evaluate, then arm the one trial */
} tfw_boot_reset_action_t;

tfw_boot_reset_action_t tfw_boot_trial_at_reset(const tfw_boot_platform_t *p);

/* PENDING -> TRIAL, before the jump. Refused unless an image is pending, and
 * refused while a trial is running or a confirm has not yet moved the floor:
 * those are handled at reset first, and arming over them would lose them. */
tfw_status_t tfw_boot_trial_arm(tfw_boot_platform_t *p);

/* TRIAL found at reset, or PENDING refused at evaluation -> TRIAL, FAILED.
 *
 * Both end the same way: boot_state TRIAL, so every later reset ends here
 * again until another OTA round replaces the image. With nothing restored, the
 * image that failed may not run, and there is no second trial. A pending image
 * refused at evaluation gets TRIAL too, although it never ran: left GOOD, a
 * later reset whose evaluation passed (an element error that cleared, say)
 * would boot it with no trial and no confirm. Idempotent, so a reset during
 * its own write repeats it. */
tfw_status_t tfw_boot_trial_fail(tfw_boot_platform_t *p);

/* CONFIRMED -> GOOD at reset, with the floor the running image's version
 * implies. *next is the floor to store: `version` where tfw_boot_floor_advance
 * allows it, `floor` otherwise. Returns tfw_boot_floor_advance's answer;
 * GOOD is set either way, because the image was confirmed and a step too large
 * for the counter is not a reason to keep re-deciding at every reset. */
tfw_status_t tfw_boot_trial_settle(tfw_boot_platform_t *p, uint32_t version,
                                   uint32_t floor, uint32_t *next);

/* --------------------------------------------- the application's transitions */
/* OTA_CONFIRM and OTA_ACTIVATE have no wire id yet (decision 8's ids are
 * PROPOSED), so these are reached only from tests until they do. */

/* TRIAL -> CONFIRMED. Refused outside a running trial, including a trial that
 * has already failed. */
tfw_status_t tfw_boot_trial_confirm(tfw_boot_platform_t *p);

/* VERIFIED -> PENDING. Refused with no verified image (a running trial is not
 * one), and refused for the image that last failed its trial: `failed` is that
 * image's measurement, or NULL when none is kept. */
tfw_status_t tfw_boot_trial_activate(tfw_boot_platform_t *p,
                                     const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                     const uint8_t *failed);

/* Whether OTA_BEGIN may start a transfer: not while an image is pending, and
 * not while a confirm has not yet moved the floor -- a reset settles it. */
tfw_status_t tfw_boot_trial_begin_permit(const tfw_boot_platform_t *p);

#endif /* TFW_BOOT_TRIAL_H */
