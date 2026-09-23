/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ed25519-donna backend: BOOT_SIG=ed25519-donna.
 *
 * A binding and nothing more. Everything above it -- the trailer, the
 * measurement, the rollback rule, the OTA state machine -- is written against
 * ag_boot_verify and does not know which primitive answers it, which is what
 * makes the size question below a question about ONE file rather than about the
 * bootloader.
 *
 * The measured cost of that primitive on this part, and why it is reported
 * rather than worked around, is in boot/boot_verify.h. Short version: 72 930
 * bytes of flash against a 4 096-byte boot section, and a 2 950-byte stack
 * frame against 2 048 bytes of SRAM.
 *
 * This file still exists, is still built and is still tested, because the
 * numbers above are a measurement and a measurement needs something to measure.
 * `make bootloader-budget` compiles it and reports what it costs; the host unit
 * suite runs it against the RFC 8032 vectors, where it is correct and where the
 * 328P's 16-bit `int` is not in the way. Deleting it would leave the claim
 * "donna does not fit" as an assertion nobody could check.
 */

#include <string.h>

#include "../boot_verify.h"
#include "ag/progmem.h"
#include "donna_shim.h"
#include "ed25519.h"

/*
 * The public key slot.
 *
 * All-0xFF is erased flash, so an unprogrammed bootloader has no key and says
 * so rather than verifying against a constant every build of this source
 * shares. The provisioning jig patches the real key in before flashing, and
 * reserved EEPROM carries its SHA-256 so that a substituted bootloader is
 * detectable -- see include/ag/boot.h.
 */
static const uint8_t pubkey_slot[AG_BOOT_PUBKEY_LEN] AG_PROGMEM = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu
};

ag_status_t ag_boot_pubkey(uint8_t out[AG_BOOT_PUBKEY_LEN])
{
    uint8_t erased = 0xFFu;
    uint8_t i;

    for (i = 0u; i < AG_BOOT_PUBKEY_LEN; i++) {
        out[i]  = (uint8_t)ag_pgm_u8(&pubkey_slot[i]);
        erased &= out[i];
    }
    if (erased == 0xFFu) {
        memset(out, 0, AG_BOOT_PUBKEY_LEN);
        return AG_ERR_NOT_PROVISIONED;
    }
    return AG_OK;
}

ag_status_t ag_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[AG_BOOT_SIG_LEN],
                           const uint8_t pub[AG_BOOT_PUBKEY_LEN])
{
    /* donna returns 0 for a good signature and -1 for everything else. The
     * inversion is here rather than at the call site so that a backend swap
     * never has to think about it again. */
    if (ed25519_sign_open(msg, (size_t)msglen, pub, sig) != 0) {
        return AG_ERR_CRYPTO;
    }
    return AG_OK;
}
