/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The measurement, and the block that carries it forward.
 *
 * This is where FWHash comes from. docs/attestation/dice-profiles.md calls it
 * signed evidence and says a verifier must be able to compute it from source --
 * so the span, the order and the chunk size below are part of the wire contract
 * even though no wire carries them. Change how this walks the flash and every
 * golden measurement a platform holds becomes wrong at once, with no symptom
 * except devices that stop verifying.
 */
#ifndef AG_BOOT_MEASURE_H
#define AG_BOOT_MEASURE_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

/*
 * SHA-256 over program memory [0, len).
 *
 * A plain hash of the bytes, in address order, with no length prefix, no domain
 * tag and no padding to a page boundary. That is deliberate and it is the
 * definition a verifier reimplements: anything cleverer is something the
 * verifier has to be told about, and the one thing a golden measurement cannot
 * survive is a convention that lives only in this file.
 *
 * `len` is bounded by AG_BOOT_MAX_IMAGE_LEN, which is the trailer's address --
 * see ag_boot_trailer_parse for why an image measuring its own signature is a
 * fixed point that does not exist.
 */
ag_status_t ag_boot_measure(uint32_t len, uint8_t out[AG_BOOT_MEASURE_LEN]);

/* SHA-256 over an arbitrary buffer, used for the public key hash. Here rather
 * than calling ag_sha256 directly so that boot/ has one dependency on the hash
 * and the size report can attribute it. */
void ag_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[AG_BOOT_MEASURE_LEN]);

/*
 * Builds the AG_BOOT_HANDOFF_LEN-byte block described in include/ag/boot.h.
 *
 * Pure, so the layout the application will parse is testable without an MCU.
 * The bootloader's own copy of the CDI is the caller's to zeroize; this
 * function does not take ownership of it, because a function that cleared its
 * caller's buffer would be a surprise the one time someone needed it twice.
 */
void ag_boot_handoff_build(uint8_t *block,
                           uint8_t tier,
                           const uint8_t cdi[AG_BOOT_MEASURE_LEN],
                           const uint8_t measurement[AG_BOOT_MEASURE_LEN],
                           uint32_t image_version);

/* Parses one back. The application's side of the same layout -- and the reason
 * the round trip is a test rather than an assumption. Returns AG_ERR_CRC on a
 * block that was never written or was overwritten by a stack that grew into it,
 * which on a part with no MPU is the failure worth naming. */
ag_status_t ag_boot_handoff_parse(const uint8_t *block,
                                  uint8_t *tier,
                                  uint8_t cdi[AG_BOOT_MEASURE_LEN],
                                  uint8_t measurement[AG_BOOT_MEASURE_LEN],
                                  uint32_t *image_version);

#endif /* AG_BOOT_MEASURE_H */
