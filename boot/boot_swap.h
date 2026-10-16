/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_BOOT_SWAP_H
#define TFW_BOOT_SWAP_H

#include <stdint.h>
#include "tfw/status.h"
#include "boot_trial.h"

/*
 * The ATmega2560's A/B swap-move (issue #75, ADR decision 7): exchanges the
 * primary and secondary slots of the board profile's TFW_SWAP_* geometry.
 * boot_swap.c on a board that has the slots; boot_swap_none.c everywhere
 * else, where the three calls main() makes change nothing.
 *
 * The record says where a swap is and which way it runs. A swap forward ends
 * "swapped": the primary holds the image on trial and the secondary the one
 * before it, until the trial settles (kept) or fails (swapped back).
 */

/* At reset, before anything reads either slot. Resumes a swap a reset cut
 * short. Then, swapped: a trial that ended unconfirmed (TFW_BOOT_RESET_FAIL)
 * falls back and boots the previous image (TFW_BOOT_RESET_BOOT); a confirmed
 * one is kept; a fall-back whose swap never started is run. Returns the
 * action main() goes on with. */
tfw_boot_reset_action_t tfw_boot_swap_at_reset(tfw_boot_platform_t *p,
                                               tfw_boot_reset_action_t act);

/* A pending image in the secondary: checked there by the whole gate
 * (tfw_boot_evaluate_at) and, passed, swapped into the primary, where the gate
 * evaluates it again. Refused: no page is written, the round ends GOOD with
 * FAILED, and the gate's status is returned. Nothing else pending, or already
 * swapped: TFW_OK, nothing done. */
tfw_status_t tfw_boot_swap_take(tfw_boot_platform_t *p, uint32_t floor);

/* The serial session's image, whole in the secondary: PENDING is stored with
 * the primary's image still GOOD, the state the store's copy leaves, and it is
 * checked and swapped in as take does. A reset from here on is main()'s path.
 * Refused, with nothing stored, unless the slots are idle. TFW_OK, nothing
 * done, on a board without the slots, where the session wrote the region in
 * place. */
tfw_status_t tfw_boot_swap_commit(tfw_boot_platform_t *p, uint32_t floor);

/* Swapped, and the image refused: the platform block falls back first, then
 * the slots swap back. TFW_ERR_BAD_STATE with nothing swapped. */
tfw_status_t tfw_boot_swap_fall_back(tfw_boot_platform_t *p);

/* The swap itself: starts one (forward, or back when swapped) or resumes the
 * one the record names, and runs it to the end. A page write's status on
 * failure, with the record naming the step to redo. */
tfw_status_t tfw_boot_swap(void);

/* 1 while the record names a swap in progress. */
uint8_t tfw_boot_swap_in_progress(void);

#endif /* TFW_BOOT_SWAP_H */
