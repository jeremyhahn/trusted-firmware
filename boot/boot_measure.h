/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The measurement, and the block that carries it forward.
 *
 * This is where FWHash comes from. docs/dice-attestation/architecture.md calls it
 * signed evidence and says a verifier must be able to compute it from source --
 * so the span, the order and the chunk size below are part of the wire contract
 * even though no wire carries them. Change how this walks the flash and every
 * golden measurement a platform holds becomes wrong at once, with no symptom
 * except devices that stop verifying.
 */
#ifndef TFW_BOOT_MEASURE_H
#define TFW_BOOT_MEASURE_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"
#include "tfw/types.h"

/*
 * SHA-256 over program memory [0, len).
 *
 * A plain hash of the bytes, in address order, with no length prefix, no domain
 * tag and no padding to a page boundary. That is deliberate and it is the
 * definition a verifier reimplements: anything cleverer is something the
 * verifier has to be told about, and the one thing a golden measurement cannot
 * survive is a convention that lives only in this file.
 *
 * `len` is bounded by TFW_BOOT_MAX_IMAGE_LEN, which is the trailer's address --
 * see tfw_boot_trailer_parse for why an image measuring its own signature is a
 * fixed point that does not exist.
 */
tfw_status_t tfw_boot_measure(uint32_t len, uint8_t out[TFW_BOOT_MEASURE_LEN]);

/* The same hash over [base, base + len) of another source: the external store
 * a staged image waits in (issue #75, boot/boot_stage.c). tfw_boot_measure is
 * this over program memory, so the two cannot hash a span differently. A read
 * that fails ends the hash with its status. */
typedef tfw_status_t (*tfw_boot_reader_t)(uint32_t addr, uint8_t *buf, uint8_t len);

tfw_status_t tfw_boot_measure_from(tfw_boot_reader_t rd, uint32_t base, uint32_t len,
                                   uint8_t out[TFW_BOOT_MEASURE_LEN]);

/* SHA-256 over an arbitrary buffer, used for the public key hash. Here rather
 * than calling tfw_sha256 directly so that boot/ has one dependency on the hash
 * and the size report can attribute it. */
void tfw_boot_hash(const uint8_t *data, uint16_t len, uint8_t out[TFW_BOOT_MEASURE_LEN]);

/*
 * Builds the TFW_BOOT_HANDOFF_LEN-byte block described in include/tfw/boot.h.
 *
 * Pure, so the layout the application will parse is testable without an MCU.
 * The bootloader's own copy of the CDI is the caller's to zeroize; this
 * function does not take ownership of it, because a function that cleared its
 * caller's buffer would be a surprise the one time someone needed it twice.
 *
 * `reset_cause` is the abstract cause the boot HAL observed before anything
 * could clear it -- issue #18, abstracted by #10. It is carried as the enum's
 * own value: the register it came from is the HAL's business, and the
 * application that reads this block decodes nothing.
 */
void tfw_boot_handoff_build(uint8_t *block,
                           uint8_t tier,
                           const uint8_t cdi[TFW_BOOT_MEASURE_LEN],
                           const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                           uint32_t image_version,
                           tfw_reset_cause_t reset_cause);

/* Puts the soft root's DeviceID seed, HMAC(UDS, TFW_DEVICEID_LABEL), into a
 * block tfw_boot_handoff_build made, and seals the CRC again. A tfw-boot with
 * no UDS never calls it, and the block carries the all-zero seed the build
 * wrote. The seed stays the caller's to zeroize, as the CDI does. */
void tfw_boot_handoff_set_seed(uint8_t *block, const uint8_t seed[TFW_BOOT_SEED_LEN]);

/* The block's crc16: every byte but its own two (include/tfw/boot.h). */
uint16_t tfw_boot_handoff_crc(const uint8_t *block);

/* Parses one back. The application's side of the same layout -- and the reason
 * the round trip is a test rather than an assumption. Returns TFW_ERR_CRC on a
 * block that was never written or was overwritten by a stack that grew into it,
 * which on a part with no MPU is the failure worth naming. Returns
 * TFW_ERR_UNSUPPORTED on a version this build does not know -- see
 * include/tfw/boot.h for why that degrades exactly like no handoff at all. */
tfw_status_t tfw_boot_handoff_parse(const uint8_t *block,
                                  uint8_t *tier,
                                  uint8_t cdi[TFW_BOOT_MEASURE_LEN],
                                  uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                  uint32_t *image_version,
                                  uint8_t *reset_cause,
                                  uint8_t seed[TFW_BOOT_SEED_LEN]);

#endif /* TFW_BOOT_MEASURE_H */
