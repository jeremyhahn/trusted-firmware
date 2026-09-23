/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image trailer, and the two questions asked of it.
 *
 * "Is this a well-formed trailer?" and "may this version run here?" are pure
 * functions of bytes and two integers. They touch no register, read no flash
 * and call nothing, which is what lets the whole of the accept/refuse decision
 * be tested on x86 -- including the cases a bench cannot easily produce, such
 * as an image_len that reaches into its own signature.
 */
#ifndef AG_BOOT_IMAGE_H
#define AG_BOOT_IMAGE_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

/* Parses AG_BOOT_TRAILER_LEN bytes into `out`.
 *
 * Refuses, rather than returning a trailer the caller must then re-check:
 *   AG_ERR_UNSUPPORTED  the magic is absent, or the trailer version is one
 *                       this bootloader does not know
 *   AG_ERR_RANGE        image_len is zero, or reaches the trailer
 *   AG_ERR_CRYPTO       image_version is zero
 *
 * image_version zero is refused because an unversioned image has no place in a
 * rollback floor: accepting one would mean accepting an image that can never be
 * superseded and never be refused. A tool that forgot to stamp a version gets a
 * clear failure instead of an image that quietly opts out of anti-rollback.
 */
ag_status_t ag_boot_trailer_parse(const uint8_t *raw, ag_boot_trailer_t *out);

/* Serialises a trailer back to bytes. The signing tool's side of the same
 * layout, and the reason the round trip is testable: a parser tested only
 * against bytes some other program produced is a parser tested against that
 * program's bugs. `signature` may be NULL, which leaves the signature zeroed --
 * that is the form the measurement and the signed span are computed over. */
void ag_boot_trailer_serialise(const ag_boot_trailer_t *tr, uint8_t *raw);

/* ------------------------------------------------------------ anti-rollback */

/*
 * THE ROLLBACK RULE, in one place, so it cannot be spelled two ways.
 *
 * Permitted when version >= floor. Equal is permitted deliberately: reflashing
 * the image already running is a repair, not a downgrade, and a rule that
 * refused it would turn a corrupted-but-current device into a brick.
 *
 * Returns AG_OK or AG_ERR_PROTECTED_REGION -- the same status the EEPROM guard
 * uses, because it is the same sentence: this is platform-owned and your
 * request does not get to change it.
 */
ag_status_t ag_boot_rollback_permit(uint32_t version, uint32_t floor);

/*
 * How far the floor may be advanced in one update, and why there is a limit.
 *
 * On the element tier the floor IS the part's monotonic counter, which advances
 * one increment at a time and has roughly two million of them for the life of
 * the device. An image stamped with version 2000000 would spend the entire
 * budget in one update and leave the device with a floor it can never raise
 * again -- anti-rollback permanently stuck, from a signed image, with no way
 * back because the counter does not decrease.
 *
 * So a jump larger than this is refused. The cost is that a device cannot skip
 * more than AG_BOOT_FLOOR_MAX_STEP security versions in a single update and
 * must be walked up through the intermediate ones. That is a real operational
 * cost, and it is smaller than the alternative, which is unrecoverable.
 */
#define AG_BOOT_FLOOR_MAX_STEP 16u

/* Computes the floor after `version` has verified and booted.
 *
 * Returns the new floor in *out and AG_OK, or AG_ERR_RANGE with *out unchanged
 * when the step is too large. A version at or below the floor leaves it alone:
 * the floor only ever goes up, which is the whole property.
 */
ag_status_t ag_boot_floor_advance(uint32_t version, uint32_t floor, uint32_t *out);

#endif /* AG_BOOT_IMAGE_H */
