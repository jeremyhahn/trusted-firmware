/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_BOOT_STAGE_H
#define TFW_BOOT_STAGE_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/*
 * Takes the image the application staged in the external OTA store (issue
 * #75, ADR decision 6.3) into the application region.
 *
 * The stager's MARK record names the staged stream; the stream is checked
 * again here, as if the application had never looked at it: the trailer it
 * ends with parses, is bound to this board, is not below `floor`, and the
 * SHA-256 over its image_len staged bytes equals both the trailer's
 * measurement and the mark's. Signature and pubkey stay with
 * tfw_boot_evaluate, which main() runs on the copied image as on any other.
 *
 * The copy writes STAGED to the platform block before the first page and
 * GOOD with ota_state PENDING after the last, so main() hands the image to
 * the one trial (boot_trial.h) exactly as a session's commit does. A reset
 * mid-copy leaves STAGED, and the next boot copies again from the start: the
 * store is never written, so the source is still there.
 *
 * Returns TFW_OK when an image was copied. Anything else copied nothing
 * (or, from a page write, left STAGED behind): TFW_ERR_UNSUPPORTED for no
 * store, TFW_ERR_BAD_STATE for no mark, an image already installed, or a
 * round that may not start (tfw_boot_trial_begin_permit), and the failing
 * check's status otherwise. Built without a store (boot_stage_none.c), it
 * always returns TFW_ERR_UNSUPPORTED.
 */
tfw_status_t tfw_boot_stage_take(tfw_boot_platform_t *p, uint32_t floor);

#endif /* TFW_BOOT_STAGE_H */
