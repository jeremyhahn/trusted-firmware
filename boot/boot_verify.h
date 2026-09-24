/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SIGNATURE VERIFICATION -- the seam, and only the seam.
 *
 * One function, substituted at link time like everything else here. The
 * backend is chosen by BOOT_SIG in mk/boot.mk and exactly one is linked.
 *
 * VERIFY ONLY. There is no sign entry point on this contract and there must
 * never be one. A bootloader that could sign would need a private key in the
 * boot section, and the boot section is the one place on this part whose
 * contents survive every update and can never be revoked without physical
 * access. The signing key belongs to the build system.
 *
 * ------------------------------------------------------------------------
 * WHAT THE 4 KB BUDGET DOES TO THIS DECISION -- measured, not estimated.
 *
 * `make bootloader-budget` reproduces every number below.
 *
 * ed25519-donna's verify path, compiled by avr-gcc 14.2 at -Os with
 * --gc-sections and nothing reachable but ed25519_sign_open:
 *
 *     flash   72 930 bytes   (68 914 .text + 4 016 .data)
 *     stack    2 950 bytes   in ed25519_sign_open's own frame alone
 *
 * The boot section on an ATmega328P is 4 096 bytes and that is the part's
 * maximum BOOTSZ -- there is no larger setting to select. The part's entire
 * flash is 32 768 bytes and its entire SRAM is 2 048. So donna's verify path is
 * 17.8x the whole boot section, 2.2x the whole part, and its single largest
 * stack frame exceeds the part's total SRAM by 902 bytes.
 *
 * It is also not merely large: `ge25519_niels_sliding_multiples` is a single
 * 3 840-byte rodata table, 94% of the boot section before one instruction of
 * code. And donna assumes a 32-bit `int` -- `reduce_mask_25 = (1 << 25) - 1`
 * in curve25519-donna-32bit.h is evaluated in `int`, which is 16 bits on AVR,
 * so the field arithmetic is wrong on this target before it is too big for it.
 *
 * An earlier design note put ed25519-donna's verify path at "about 2 KB".
 * That number is wrong by a factor of thirty-six; the measured one is in
 * docs/secure-boot/engineering.md.
 *
 * NO SUBSTITUTE HAS BEEN MADE. Picking a different primitive is an
 * architectural decision with a security cost attached to every option, and it
 * is not one to take quietly inside a seam. The alternatives, with their costs,
 * are in docs/secure-boot/engineering.md.
 * ------------------------------------------------------------------------
 */
#ifndef AG_BOOT_VERIFY_H
#define AG_BOOT_VERIFY_H

#include <stdint.h>
#include "ag/boot.h"
#include "ag/status.h"

/*
 * Verifies `sig` over `msg` under `pub`.
 *
 * Returns AG_OK only on a valid signature. Every other outcome -- a malformed
 * point, a non-canonical scalar, a backend that failed internally -- is
 * AG_ERR_CRYPTO, because the caller's only correct response to any of them is
 * the same one and a caller given three failure statuses is a caller that will
 * eventually treat one of them as recoverable.
 */
ag_status_t ag_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[AG_BOOT_SIG_LEN],
                           const uint8_t pub[AG_BOOT_PUBKEY_LEN]);

/*
 * The public key this bootloader verifies against.
 *
 * In boot-section flash beside the code that uses it, where BLB mode 3 puts it
 * out of the application's reach, and pinned by the SHA-256 in reserved EEPROM
 * -- see include/ag/boot.h for why the key is in one place and its hash in the
 * other. Returns AG_ERR_NOT_PROVISIONED when the slot is still erased.
 */
ag_status_t ag_boot_pubkey(uint8_t out[AG_BOOT_PUBKEY_LEN]);

#endif /* AG_BOOT_VERIFY_H */
