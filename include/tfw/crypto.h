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
#ifndef TFW_CRYPTO_H
#define TFW_CRYPTO_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"

#define TFW_SHA256_LEN 32u
#define TFW_SIG_LEN    64u

tfw_status_t      tfw_crypto_init(void);

/* What this build's root of trust actually is. */
tfw_crypto_tier_t tfw_crypto_tier(void);

/* A bitmap of TFW_ALG_* values this provider can sign with. Reported in CAPS so
 * the host negotiates rather than assumes -- and so that adding an element with
 * hardware Ed25519 is a provisioning change, not a protocol change. */
uint16_t         tfw_crypto_algorithms(void);

/* Fills out with len random bytes.
 *
 * Returns TFW_ERR_CRYPTO on a tier with no entropy source rather than returning
 * something that merely looks random. A predictable nonce presented as a random
 * one is worse than an error, because the error stops the protocol and the
 * prediction does not. */
tfw_status_t      tfw_crypto_random(uint8_t *out, uint8_t len);

tfw_status_t      tfw_crypto_sha256(const uint8_t *data, uint16_t len,
                                  uint8_t out[TFW_SHA256_LEN]);

/* HMAC-SHA256 under a provider-held key. `slot` names the key; on the software
 * tier it is an index into flash, on an element it is a slot number the key
 * never leaves.
 *
 * This is the DICE one-way function: the hardware requirements spec gives
 * HMAC(UDS, H(first mutable code)) as the stronger of its two forms, because an
 * HMAC's security is the full width of its key where a bare hash's is half its
 * digest. */
tfw_status_t      tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                                uint8_t out[TFW_SHA256_LEN]);

/* Signs a digest. Writes the algorithm actually used into alg_id, because the
 * caller asked for a signature and not for a particular algorithm -- the
 * provider knows which it has. */
tfw_status_t      tfw_crypto_sign(const uint8_t digest[TFW_SHA256_LEN],
                                uint8_t sig[TFW_SIG_LEN], uint8_t *alg_id);

/* The public half of the signing key, or TFW_ERR_NOT_PROVISIONED. */
tfw_status_t      tfw_crypto_pubkey(uint8_t out[TFW_SIG_LEN]);

/* A monotonic counter, for the boot count and firmware anti-rollback. */
tfw_status_t      tfw_crypto_counter(uint8_t idx, uint32_t *out);

/*
 * Verifies a signature over `digest` under an EXTERNAL public key -- the
 * AUTH_CHALLENGE/AUTH_RESPONSE session's operator key (issue #4), never this
 * device's own identity key. `pubkey` travels in the call; nothing about it
 * is stored in, or protected by, this provider the way the DeviceID and UDS
 * are, which is exactly why it is a separate entry point and not a mode of
 * tfw_crypto_sign.
 *
 * TFW_ERR_UNSUPPORTED on a tier with no external-key verify path. That is
 * every tier but the secure element today: the software tier's only available
 * primitive for it, ed25519-donna (already vendored for BOOT_SIG), does not
 * fit this device family -- see docs/security/threat-model.md and
 * changelog.d/ for the measurement that ruled it out. TFW_ERR_CRYPTO on a
 * signature that does not verify or an element that will not answer.
 */
tfw_status_t      tfw_crypto_verify_operator(const uint8_t digest[TFW_SHA256_LEN],
                                           const uint8_t sig[TFW_SIG_LEN],
                                           const uint8_t pubkey[TFW_SIG_LEN]);

/* Verify an ES256 signature over `digest` against the board id's
 * image-signing key -- the same stored key tfw-boot checks the trailer
 * against, so one P-256 key per board id verifies both the trailer and a SUIT
 * manifest (issue #78). The key is not passed: it is provisioned, and an
 * application that could name the key it is checked against could name its
 * own.
 *
 * TFW_ERR_UNSUPPORTED on a tier with no stored image key -- every tier but the
 * secure element. TFW_ERR_CRYPTO on a signature that does not verify or an
 * element that will not answer. */
tfw_status_t      tfw_crypto_verify_image(const uint8_t digest[TFW_SHA256_LEN],
                                        const uint8_t sig[TFW_SIG_LEN]);

#endif /* TFW_CRYPTO_H */
