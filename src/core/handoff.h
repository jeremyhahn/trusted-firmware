/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application's side of the DICE CDI handoff -- issue #6/#18.
 *
 * tfw_boot_handoff_init() is called exactly ONCE, early in main(), before
 * tfw_crypto_init(): it reads tfw_hal_handoff_block(), parses it with
 * boot/boot_handoff.c's tfw_boot_handoff_parse (the bootloader's own decoder,
 * reused rather than duplicated), copies out what a valid block held, and then
 * zeroizes the raw SRAM block -- consumed once, exactly as
 * include/tfw/boot.h's handoff section requires.
 *
 * A missing or corrupted block -- no bootloader ran, a stock bootloader ran,
 * or a stack grew into it -- is not an error here: it is the "unprovisioned"
 * case every accessor below reports plainly, the same way an absent UDS is.
 */
#ifndef TFW_CORE_HANDOFF_H
#define TFW_CORE_HANDOFF_H

#include <stdint.h>
#include "tfw/boot.h"

void tfw_boot_handoff_init(void);

/* The measurement tfw-boot actually verified, or NULL when no valid handoff
 * was found. This is FWHash -- src/caps/attest.c reads it directly and does
 * not recompute anything. */
const uint8_t *tfw_boot_handoff_measurement(void);   /* TFW_BOOT_MEASURE_LEN bytes, or NULL */

/* FWHash as evidence reports it: the measurement above, or TFW_BOOT_MEASURE_LEN
 * zero bytes when there is none. Never NULL. The zeros are the handoff's own
 * buffer, which is all-zero whenever no valid handoff was found, so no image
 * keeps a second 32-byte constant for the same answer. */
const uint8_t *tfw_boot_handoff_fw_hash(void);

/* 1 and *out set when a valid handoff carried a reset cause; 0 otherwise, in
 * which case the caller's own .init3 observation is the only one there is --
 * see src/hal/avr/hal_sys.c. */
uint8_t tfw_boot_handoff_reset_cause(uint8_t *out);

/* The tier tfw-boot itself observed, or NULL when there is no valid handoff to
 * ask -- a crypto provider deciding whether to trust the CDI below cares
 * whether the bootloader agrees with what it is about to derive. */
const uint8_t *tfw_boot_handoff_tier(void);

/*
 * CONSUMING. Copies the handed-off CDI into `out` and zeroizes this module's
 * own copy of it -- there is exactly one call site per image, the crypto
 * provider's own tfw_crypto_init(), and after that call nothing here holds it
 * any longer. Returns 0 when there was no valid handoff (`out` is untouched),
 * 1 otherwise.
 */
uint8_t tfw_boot_handoff_take_cdi(uint8_t out[TFW_BOOT_MEASURE_LEN]);

#endif /* TFW_CORE_HANDOFF_H */
