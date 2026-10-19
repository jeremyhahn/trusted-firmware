/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Layer 0's digest verify on the p256-soft row (#48): the entry the SUIT
 * manifest's COSE_Sign1 check reaches through tfw/crypto.h, since
 * src/core/suit.c hashes the Sig_structure itself and passes the digest.
 * The check is boot/verify/verify_p256_soft.c's -- P-256 ECDSA (FIPS 186-5
 * section 6.4.2) of a 64-byte r || s, with the key the bootloader carries --
 * over a digest already taken. That file is the AVR boot's and stays as it is;
 * the ROM row has this entry in verify_p256_rom.c. The curve arithmetic is
 * micro-ecc's: ESP-IDF's component in the bootloader, as for
 * verify_p256_soft.c, and src/third_party/micro-ecc in the host tests.
 */
#include "tfw/crypto.h"
#include "tfw/progmem.h"
#include "uECC.h"

/* p256_key_slot.c, or the host test's own key: X || Y, big-endian. */
extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

tfw_status_t tfw_crypto_verify_image(const uint8_t digest[TFW_SHA256_LEN],
                                     const uint8_t sig[TFW_SIG_LEN])
{
    uint8_t key[64];
    uint8_t erased = 0xFFu;
    uint8_t i;

    for (i = 0u; i < 64u; i++) {
        key[i]  = (uint8_t)tfw_pgm_far_u8(tfw_boot_p256_key, &tfw_boot_p256_key[i]);
        erased &= key[i];
    }
    /* No key provisioned is a refusal, never a pass. */
    if (erased == 0xFFu) {
        return TFW_ERR_CRYPTO;
    }
    return uECC_verify(key, digest, TFW_SHA256_LEN, sig, uECC_secp256r1()) == 1
               ? TFW_OK : TFW_ERR_CRYPTO;
}
