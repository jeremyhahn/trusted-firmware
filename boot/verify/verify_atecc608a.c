/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATECC608A backend: BOOT_SIG=atecc608a.
 *
 * P-256 ECDSA verification happens in silicon. The boot section frames two
 * commands and holds no curve arithmetic, no field arithmetic and no scalar
 * tables -- which is the only arrangement under which an asymmetric signature
 * fits a 4 KB boot section on this family. ed25519-donna's verify path is
 * 72 930 bytes; this is a few hundred.
 *
 * VERIFY IN STORED MODE, NOT EXTERNAL, AND THE DIFFERENCE IS THE POINT.
 *
 * External mode takes the public key in the command packet. That would put the
 * signing key in boot-section flash, where an attacker who can rewrite flash
 * can substitute their own and sign whatever they like -- and the whole reason
 * to verify at boot is to survive an attacker who has reached the flash.
 *
 * Stored mode names a SLOT. The public key lives in the element, written at
 * provisioning and locked, and after that there is no command on the part that
 * can change it. Rewriting every byte of the MCU's flash does not help: the
 * element still verifies against the key the manufacturer put there, and an
 * image signed by anyone else is refused.
 *
 * It is also smaller. Stored mode sends 64 bytes of signature where external
 * mode sends 128 of signature and key, which keeps the command inside the
 * driver's existing buffer rather than growing it for every build.
 */

#include <string.h>

#include "../boot_measure.h"
#include "../boot_verify.h"
#include "atecc.h"

/* Element command constants for Verify. Facts about the silicon. */
#define OP_VERIFY          0x45u
#define VERIFY_MODE_STORED 0x00u
#define VERIFY_KEY_P256    0x0004u

/*
 * The slot holding the image-signing public key.
 *
 * Not the DeviceID slot. The DeviceID is this device's own identity and its
 * private half is in the element; this is the BUILD SYSTEM's public key, the
 * same value on every device from one production line, and it is public. They
 * are different keys with different lifetimes and conflating them would mean
 * that revoking a signing key required re-provisioning every device's identity.
 */
#define SLOT_IMAGE_SIGNING 2u

tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    /*
     * There is nothing to return, and that is not a gap.
     *
     * The key never leaves the element and the bootloader never holds it, so
     * there is no buffer for this contract to fill. The caller does not need it
     * either: tfw_boot_verify answers the only question anyone asks of a public
     * key, which is whether a signature is good. A backend that handed the key
     * out would invite a caller to verify with it in software, which is the
     * arrangement this backend exists to avoid.
     */
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

/* ES256: SHA-256, then the element's P-256 verify. */
const uint8_t tfw_boot_cose_alg = 0x26u;

tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    uint8_t     digest[32];
    tfw_status_t st;

    /* The key is the element's, named by slot. A caller that supplied one is
     * using this backend wrongly, and ignoring it silently would let a
     * substituted key look as if it had been honoured. */
    if (pub != 0) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (msg == 0 || sig == 0) {
        return TFW_ERR_CRYPTO;
    }

    /*
     * ECDSA signs a digest, and the element verifies what is in TempKey.
     *
     * THROUGH tfw_boot_hash, WHICH IS THE BUILD'S ONE HASH, and not tfw_sha256.
     * The reason is the whole reason BOOT_MEASURE=atecc608a exists: calling the
     * software hash from here links src/crypto/sha256.c into the boot section,
     * which is 4 052 bytes against a 4 096-byte section -- so the 328P
     * configuration does not link at all, and nothing noticed only because
     * tfw_boot_verify was unreachable and LTO deleted the call.
     *
     * The cost argument that used to sit here -- a bus round trip per 64-byte
     * block is seconds of boot time -- is about the IMAGE, which is tens of
     * kilobytes. This message is TFW_BOOT_TR_SIGNED_LEN bytes: one Start and one
     * End, once per boot. It was the right argument attached to the wrong call.
     */
    tfw_boot_hash(msg, msglen, digest);

    if (tfw_atecc_wake() != TFW_OK) {
        /* An element that does not answer is not a pass. A bootloader that
         * booted anyway when its verifier was absent would be one an attacker
         * defeats by cutting two traces. */
        return TFW_ERR_CRYPTO;
    }

    st = tfw_atecc_exec(TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u,
                       digest, 32u, 0, 0u, TFW_ATECC_T_NONCE);
    if (st == TFW_OK) {
        /* A status response of zero is a valid signature. Anything else --
         * including the element's own "miscompare" -- is a refusal, and they
         * are deliberately not distinguished: the caller's correct response to
         * every one of them is identical, and a caller handed three failure
         * statuses is one that will eventually treat one as recoverable. */
        st = tfw_atecc_exec(OP_VERIFY, VERIFY_MODE_STORED,
                           (uint16_t)SLOT_IMAGE_SIGNING,
                           sig, TFW_BOOT_SIG_LEN, 0, 0u, TFW_ATECC_T_VERIFY);
    }
    (void)tfw_atecc_sleep();
    return st == TFW_OK ? TFW_OK : TFW_ERR_CRYPTO;
}
