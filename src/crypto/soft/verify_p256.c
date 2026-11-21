/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw_crypto_verify_operator in software: ES256's P-256 ECDSA verify over a
 * digest the caller computed (FIPS 186-5 section 6.4.2, SEC 1 v2 section
 * 4.1.4), the same check boot/verify/verify_p256_soft.c makes on an image. The
 * curve arithmetic is micro-ecc v1.2, vendored in src/third_party/micro-ecc;
 * nothing here reimplements it.
 *
 * LINKED IN TWO PLACES ONLY. Where the tamper-response engine is linked (#50,
 * mk/firmware.mk HSM_SRCS), and, renamed tfw_crypto_soft_verify_p256, into an
 * architecture provider that wraps the soft one (the board profile's
 * ARCH_CRYPTO_WRAPS_SOFT, the STM32H5's, #544), which falls back to it. The
 * soft provider refuses this call on every other image (provider.c, issue
 * #4), and those images keep refusing it: on an HSM image mk/firmware.mk
 * renames the provider's refusal away, so this file's definition is the one the
 * SUIT reader (src/core/suit_integrated.c) calls to check a policy set under the
 * tenant key. AUTH_CHALLENGE is not on a soft image either way (auth_session_
 * none.c), so this file opens no operator session.
 */
#include "tfw/crypto.h"
#include "uECC.h"

tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[32],
                                      const uint8_t sig[64],
                                      const uint8_t pubkey[64])
{
    if (digest == 0 || sig == 0 || pubkey == 0) {
        return TFW_ERR_CRYPTO;
    }
    /* A point off the curve is refused before it reaches the verify. */
    if (uECC_valid_public_key(pubkey, uECC_secp256r1()) != 1) {
        return TFW_ERR_CRYPTO;
    }
    return uECC_verify(pubkey, digest, 32u, sig, uECC_secp256r1()) == 1
         ? TFW_OK : TFW_ERR_CRYPTO;
}
