/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image trailer, and the three questions asked of it.
 *
 * "Is this a well-formed trailer?", "was it built for this board?" and "may
 * this version run here?" are pure functions of bytes and integers. They touch no register, read no flash
 * and call nothing, which is what lets the whole of the accept/refuse decision
 * be tested on x86 -- including the cases a bench cannot easily produce, such
 * as an image_len that reaches into its own signature.
 */
#ifndef TFW_BOOT_IMAGE_H
#define TFW_BOOT_IMAGE_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

/* Parses TFW_BOOT_TRAILER_LEN bytes into `out`.
 *
 * Refuses, rather than returning a trailer the caller must then re-check:
 *   TFW_ERR_UNSUPPORTED  the magic is absent, or the trailer version is one
 *                       this bootloader does not know
 *   TFW_ERR_RANGE        image_len is zero, or reaches the trailer
 *   TFW_ERR_CRYPTO       image_version is zero
 *
 * image_version zero is refused because an unversioned image has no place in a
 * rollback floor: accepting one would mean accepting an image that can never be
 * superseded and never be refused. A tool that forgot to stamp a version gets a
 * clear failure instead of an image that quietly opts out of anti-rollback.
 */
tfw_status_t tfw_boot_trailer_parse(const uint8_t *raw, tfw_boot_trailer_t *out);

/* Serialises a trailer back to bytes. The signing tool's side of the same
 * layout, and the reason the round trip is testable: a parser tested only
 * against bytes some other program produced is a parser tested against that
 * program's bugs. `signature` may be NULL, which leaves the signature zeroed --
 * that is the form the measurement and the signed span are computed over. */
void tfw_boot_trailer_serialise(const tfw_boot_trailer_t *tr, uint8_t *raw);

/* ------------------------------------------------------------ the binding */

/*
 * MAY THIS IMAGE RUN ON THIS BOARD? `board_id` is the part's own TFW_BOARD_ID,
 * passed in rather than read here so the decision stays a pure function of
 * its inputs and every refusal is reachable from a host test.
 *
 * Returns TFW_ERR_UNSUPPORTED, as for a trailer version this bootloader does
 * not know, when:
 *   - the trailer's board_id is 0, which names no board and is what every
 *     image built before the field existed carries;
 *   - the trailer's board_id is not this board's -- a genuine image, signed by
 *     the key for another board id, is still not an image for this part;
 *   - an image_flags bit outside TFW_BOOT_IMAGE_FLAGS_KNOWN is set.
 *
 * TFW_BOOT_IMAGE_FLAG_DEV is parsed and carried, and not judged here: what a
 * development image may do on a production key is a policy of its own.
 *
 * The check is not an authentication: the signature, verified last, is what
 * makes board_id trustworthy. Run before the floor it only refuses early, and
 * a refusal changes nothing.
 */
tfw_status_t tfw_boot_binding_permit(const tfw_boot_trailer_t *tr, uint8_t board_id);

/* ------------------------------------------------------------ anti-rollback */

/*
 * THE ROLLBACK RULE, in one place, so it cannot be spelled two ways.
 *
 * Permitted when version >= floor. Equal is permitted deliberately: reflashing
 * the image already running is a repair, not a downgrade, and a rule that
 * refused it would turn a corrupted-but-current device into a brick.
 *
 * Returns TFW_OK or TFW_ERR_PROTECTED_REGION -- the same status the EEPROM guard
 * uses, because it is the same sentence: this is platform-owned and your
 * request does not get to change it.
 */
tfw_status_t tfw_boot_rollback_permit(uint32_t version, uint32_t floor);

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
 * more than TFW_BOOT_FLOOR_MAX_STEP security versions in a single update and
 * must be walked up through the intermediate ones. That is a real operational
 * cost, and it is smaller than the alternative, which is unrecoverable.
 */
#define TFW_BOOT_FLOOR_MAX_STEP 16u

/* Computes the floor after `version` has verified and booted.
 *
 * Returns the new floor in *out and TFW_OK, or TFW_ERR_RANGE with *out unchanged
 * when the step is too large. A version at or below the floor leaves it alone:
 * the floor only ever goes up, which is the whole property.
 */
tfw_status_t tfw_boot_floor_advance(uint32_t version, uint32_t floor, uint32_t *out);

#endif /* TFW_BOOT_IMAGE_H */
