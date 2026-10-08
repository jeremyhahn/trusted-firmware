/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE BOOT DECISION, in one function.
 *
 * Every reason to refuse an image lives here, in one order, so that "why did
 * this device not boot" has one answer and one place to read it. The
 * alternative -- checks scattered through boot_main.c between the peripheral
 * setup -- is how a check ends up being skipped on one path and nobody notices,
 * because the path that skips it is the one that works.
 *
 * It reaches hardware only through tfw_boot_flash_read and the verify seam, both
 * of which the host supplies, so the whole decision runs on x86. That is what
 * makes "a tampered image refuses" and "a downgrade is refused" testable
 * against the real logic rather than against a description of it.
 */
#ifndef TFW_BOOT_POLICY_H
#define TFW_BOOT_POLICY_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"

typedef struct {
    tfw_boot_trailer_t trailer;
    uint8_t measurement[TFW_BOOT_MEASURE_LEN];  /* what the flash actually hashes to */
} tfw_boot_verdict_t;

/*
 * Decides whether the application in flash may run.
 *
 * TFW_OK, or the first reason it may not:
 *
 *   TFW_ERR_UNSUPPORTED       no trailer, or one from a layout this build does
 *                            not know -- typically a blank part
 *   TFW_ERR_RANGE             a trailer whose image_len is impossible
 *   TFW_ERR_PROTECTED_REGION  a downgrade: the version is below the floor
 *   TFW_ERR_CRYPTO            the flash does not hash to the recorded
 *                            measurement, or the signature does not verify
 *   TFW_ERR_NOT_PROVISIONED   this bootloader has no public key, or the key it
 *                            has is not the one reserved EEPROM pins
 *
 * A backend that keeps its key -- the element, verifying in stored mode -- has
 * no key to pin and is not refused for it. See the TFW_ERR_UNSUPPORTED arm in
 * boot_policy.c, and note that this is the ONLY configuration that fits a 4 KB
 * boot section, so the arm is on every supported build rather than an exotic
 * one.
 *
 * ORDER. The rollback check comes before the signature check, because it is
 * cheap and refusing early costs nothing -- and because it is safe: the version
 * it reads is inside the span the signature covers, so a forged version cannot
 * make this function ACCEPT anything. It can only make it refuse, which is the
 * direction that does no harm. Nothing acts on the version until after the
 * signature has verified; see tfw_boot_floor_advance's caller.
 *
 * The measurement comparison comes before the signature check for the same
 * reason and with a second benefit: on a tampered image it fails without ever
 * running the signature primitive, which on this part is by far the most
 * expensive thing the bootloader can do.
 */
tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out);

/*
 * Confirms the bootloader's own public key is the one this device was
 * provisioned to trust: SHA-256 over the 32-byte key, compared against the hash
 * in reserved EEPROM.
 *
 * Without this, replacing the bootloader replaces the root of trust, and a
 * device whose boot section was rewritten would verify happily against the
 * attacker's key. The EEPROM hash is what makes a bootloader swap detectable
 * from inside the bootloader.
 *
 * TFW_ERR_UNSUPPORTED means the backend keeps its key and will not produce one,
 * so there is nothing here to hash and nothing to pin -- see tfw_boot_evaluate,
 * which is the only caller that has to know what to do about it. It is a tier
 * capability answer, not a refusal, and it is deliberately the same shape as
 * tfw_boot_floor_read's on a tier that keeps no floor of its own.
 */
tfw_status_t tfw_boot_check_pubkey_pin(const uint8_t pinned[TFW_BOOT_MEASURE_LEN],
                                     uint8_t pubkey_out[TFW_BOOT_PUBKEY_LEN]);

/* Constant-time equality over n bytes. Returns 1 when equal.
 *
 * Constant time for the measurement comparison specifically: an early-exit
 * memcmp leaks, through timing, how many leading bytes of a forged measurement
 * are right -- which turns finding a colliding prefix into a byte-at-a-time
 * search. The signature check behind it makes that useless in theory; the
 * comparison is cheap enough that relying on "in theory" is not worth it. */
uint8_t tfw_boot_equal(const uint8_t *a, const uint8_t *b, uint8_t n);

#endif /* TFW_BOOT_POLICY_H */
