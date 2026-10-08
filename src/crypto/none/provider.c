/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE UNATTESTED TIER.
 *
 * TFW_CRYPTO_NONE has been in tfw_crypto_tier_t since the protocol was written,
 * described there as "no provider: unattested". This is the implementation of
 * it, and it exists because a part turned up that cannot honestly report
 * anything else.
 *
 * WHAT IT IS FOR, precisely. The software tier's definition is "the identity
 * key lives in flash behind lock bits" -- which requires a boot section to put
 * the key in and BLB lock bits to keep the application out of it. The ATtiny85
 * has neither: no boot-loader section, no BOOTRST, no boot lock bits. Linking
 * the software tier there would make CAPS report TFW_CRYPTO_SOFTWARE on a part
 * where the sentence that tier stands for is false, and a verifier reads that
 * byte to decide what evidence from the device is worth.
 *
 * So this is not "the software tier with the crypto removed to save space".
 * It is the answer to a question the other tiers cannot answer truthfully on
 * some silicon, and the space it saves -- SHA-256 and HMAC are over half the
 * flash of an 8 KB part -- is a consequence rather than the reason.
 *
 * WHAT A HOST SEES. CAPS reports tier 0 and an empty algorithm bitmap, so a
 * host knows before it asks that there is no root of trust here. TFW_FEATURE_
 * ATTEST is 0 on any board using this tier, so ATTEST is not even in the
 * command table and answers TFW_ERR_UNKNOWN_CMD. There is no path by which a
 * device on this tier produces something that could be mistaken for evidence.
 *
 * EVERY OPERATION REFUSES, and none of them approximates. tfw_crypto_sha256
 * could be implemented -- hashing needs no key -- and deliberately is not: it
 * would pull in the very implementation this tier exists to leave out, to
 * provide a digest with nothing to sign it. A caller that wants a hash and
 * cannot have one gets TFW_ERR_UNSUPPORTED and knows where it stands.
 *
 * Link-time substitution, exactly as the other two tiers are: the Makefile
 * links one provider per image, there is no vtable and no runtime selection, so
 * an image built for this tier cannot be talked into signing anything.
 */

#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "../../core/handoff.h"
#include "../../core/secure_zero.h"

tfw_status_t tfw_crypto_init(void)
{
    /*
     * Drains and discards any CDI tfw-boot handed off, even on a tier that
     * signs with nothing and derives nothing. A board that links this
     * provider still links the real src/core/handoff.c whenever it has a
     * boot section (mk/avr.mk's IDENTITY_SRCS is chosen by RAM_END, not by
     * CRYPTO) -- CRYPTO=none is a valid, if unusual, build for any such
     * board, not only the ATtiny85's own stubbed-out case. Without this, a
     * CDI that DID get handed off would sit resident in that module's static
     * storage for the process lifetime, honoured by neither of the other two
     * providers' "drain it even with no use for it" rule and by this one not
     * at all -- the exact gap that rule exists to close everywhere.
     */
    {
        uint8_t discard[32];
        if (tfw_boot_handoff_take_cdi(discard)) {
            tfw_secure_zero(discard, sizeof discard);
        }
    }
    /* Nothing else to derive. A device with no identity still boots, and
     * says so. */
    return TFW_OK;
}

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return TFW_CRYPTO_NONE;
}

uint16_t tfw_crypto_algorithms(void)
{
    /* Empty, and that is the point: a host negotiates against this and finds
     * nothing, rather than selecting an algorithm the device then fails to
     * perform at the moment it matters most. */
    return 0u;
}

tfw_status_t tfw_crypto_random(uint8_t *out, uint8_t len)
{
    (void)out; (void)len;
    /* No AVR in this family has a TRNG, and this tier has no CDI to stretch
     * either. A predictable nonce presented as a random one is worse than a
     * refusal: the refusal stops the protocol and the prediction does not. */
    return TFW_ERR_CRYPTO;
}

tfw_status_t tfw_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t out[TFW_SHA256_LEN])
{
    (void)data; (void)len; (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_hmac(uint8_t slot, const uint8_t *data, uint16_t len,
                           uint8_t out[TFW_SHA256_LEN])
{
    (void)slot; (void)data; (void)len; (void)out;
    /* TFW_ERR_UNSUPPORTED and not TFW_ERR_NOT_PROVISIONED. The difference is
     * actionable: "not provisioned" invites a host to go and provision the
     * device, and there is nothing here to provision. */
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_sign(const uint8_t digest[TFW_SHA256_LEN],
                           uint8_t sig[TFW_SIG_LEN], uint8_t *alg_id)
{
    (void)digest; (void)sig; (void)alg_id;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_pubkey(uint8_t out[TFW_SIG_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_crypto_counter(uint8_t idx, uint32_t *out)
{
    (void)idx; (void)out;
    return TFW_ERR_UNSUPPORTED;
}

/* issue #4: no operator-signature session on the tier with no crypto at
 * all. AUTH_CHALLENGE/AUTH_RESPONSE are not compiled into a CRYPTO=none image
 * (mk/avr.mk links auth_session_none.c), so this is unreachable in practice --
 * present only because every provider implements the whole contract. */
tfw_status_t tfw_crypto_verify_operator(const uint8_t digest[TFW_SHA256_LEN],
                                      const uint8_t sig[TFW_SIG_LEN],
                                      const uint8_t pubkey[TFW_SIG_LEN])
{
    (void)digest; (void)sig; (void)pubkey;
    return TFW_ERR_UNSUPPORTED;
}
