/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE CRYPTO PROVIDER CONTRACT.
 *
 * Link-time substitution, exactly as the HAL is, and for the same reasons: one
 * implementation is linked per image, a vtable would cost RAM and defeat
 * --gc-sections, and a missing symbol is a link error rather than a runtime
 * surprise.
 *
 * Three implementations, in increasing order of what they can honestly claim:
 *
 *   src/crypto/soft/       SHA-256 and HMAC in software. The UDS lives in
 *                          boot-section flash behind lock bits. There is no
 *                          TRNG and no key isolation, so this tier reports
 *                          notSecure and means it.
 *
 *   src/crypto/atecc608a/  An ATECC608A holds the UDS in a slot configured
 *                          never-readable, computes the CDI without it ever
 *                          leaving the chip, and supplies a real TRNG. This is
 *                          the tier at which the DICE hardware requirements are
 *                          substantially met rather than approximated.
 *
 *   src/crypto/tpm2/       A discrete TPM 2.0. Strictly better again: a
 *                          manufacturer-issued endorsement chain, so a stranger
 *                          can verify the root rather than trusting our factory.
 *
 * The tier is reported in CAPS so the platform knows what it is talking to
 * BEFORE it trusts anything the device says. That ordering is the point: a
 * verifier that discovers the assurance level after accepting the evidence has
 * already accepted it.
 */
#ifndef AG_CRYPTO_H
#define AG_CRYPTO_H

#include <stdint.h>
#include "ag/status.h"
#include "ag/types.h"

#define AG_SHA256_LEN 32u
#define AG_SIG_LEN    64u

ag_status_t      ag_crypto_init(void);

/* What this build's root of trust actually is. */
ag_crypto_tier_t ag_crypto_tier(void);

/* A bitmap of AG_ALG_* values this provider can sign with. Reported in CAPS so
 * the host negotiates rather than assumes -- and so that adding an element with
 * hardware Ed25519 is a provisioning change, not a protocol change. */
uint16_t         ag_crypto_algorithms(void);

/* Fills out with len random bytes.
 *
 * Returns AG_ERR_CRYPTO on a tier with no entropy source rather than returning
 * something that merely looks random. A predictable nonce presented as a random
 * one is worse than an error, because the error stops the protocol and the
 * prediction does not. */
ag_status_t      ag_crypto_random(uint8_t *out, uint8_t len);

ag_status_t      ag_crypto_sha256(const uint8_t *data, uint16_t len,
                                  uint8_t out[AG_SHA256_LEN]);

/* HMAC-SHA256 under a provider-held key. `slot` names the key; on the software
 * tier it is an index into flash, on an element it is a slot number the key
 * never leaves.
 *
 * This is the DICE one-way function: the hardware requirements spec gives
 * HMAC(UDS, H(first mutable code)) as the stronger of its two forms, because an
 * HMAC's security is the full width of its key where a bare hash's is half its
 * digest. */
ag_status_t      ag_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                                uint8_t out[AG_SHA256_LEN]);

/* Signs a digest. Writes the algorithm actually used into alg_id, because the
 * caller asked for a signature and not for a particular algorithm -- the
 * provider knows which it has. */
ag_status_t      ag_crypto_sign(const uint8_t digest[AG_SHA256_LEN],
                                uint8_t sig[AG_SIG_LEN], uint8_t *alg_id);

/* The public half of the signing key, or AG_ERR_NOT_PROVISIONED. */
ag_status_t      ag_crypto_pubkey(uint8_t out[AG_SIG_LEN]);

/* A monotonic counter, for the boot count and firmware anti-rollback. */
ag_status_t      ag_crypto_counter(uint8_t idx, uint32_t *out);

#endif /* AG_CRYPTO_H */
