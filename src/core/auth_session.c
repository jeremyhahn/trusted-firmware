/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The operator-signature auth session, element tier -- issue #4. See
 * auth_session.h for the shape of the whole thing; this file is the one that
 * actually holds a nonce and a flag.
 */
#include <string.h>

#include "auth_session.h"
#include "platform_id.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "tfw/progmem.h"

/* Domain-separates the signed message from anything else an operator key
 * might ever be asked to sign -- and from ATTEST's and ATTEST_CERT's own
 * nonce-bound messages, which this is not and must not be confusable with. */
static const char TFW_AUTH_DOMAIN[] TFW_PROGMEM = "tfw-auth-v1";
#define TFW_AUTH_DOMAIN_LEN (sizeof(TFW_AUTH_DOMAIN) - 1u)   /* drop the NUL */

static uint8_t nonce[TFW_AUTH_NONCE_LEN];
static uint8_t challenge_pending;
static uint8_t authenticated;

void tfw_auth_session_init(void)
{
    memset(nonce, 0, sizeof nonce);
    challenge_pending = 0u;
    authenticated     = 0u;
}

tfw_status_t tfw_auth_gate(void)
{
    return authenticated ? TFW_OK : TFW_ERR_AUTH_REQUIRED;
}

tfw_status_t tfw_auth_cmd_challenge(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)r;

    /* A fresh challenge drops any session already granted. Asking for one is
     * the host declaring it wants to re-prove itself, and an old grant must
     * not survive that untouched -- whatever the element answers next. */
    authenticated = 0u;

    /* Nonces from the provider's RNG -- the element's real TRNG on this
     * tier, never anything this file invents. TFW_ERR_CRYPTO (not a nonce that
     * merely looks random) if the element will not answer. */
    if (tfw_crypto_random(nonce, TFW_AUTH_NONCE_LEN) != TFW_OK) {
        challenge_pending = 0u;
        return TFW_ERR_CRYPTO;
    }
    challenge_pending = 1u;

    tfw_write_blob(w, nonce, TFW_AUTH_NONCE_LEN);
    return TFW_OK;
}

tfw_status_t tfw_auth_cmd_response(tfw_reader_t *r, tfw_writer_t *w)
{
    const uint8_t *sig;
    uint8_t        msg[TFW_AUTH_DOMAIN_LEN + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];
    uint8_t        digest[TFW_SHA256_LEN];
    uint8_t        pubkey[TFW_BOOT_OPERATOR_PUBKEY_LEN];
    tfw_status_t    st;

    (void)w;

    /* The dispatcher already enforced TFW_AUTH_SIG_LEN exactly (commands.def's
     * min/max for this id), so no arity check belongs here -- see
     * src/core/commands.def's own header for why that check lives in
     * exactly one place. */
    sig = tfw_read_blob(r, TFW_AUTH_SIG_LEN);
    if (!sig) {
        return TFW_ERR_BAD_ARGC;
    }

    if (!challenge_pending) {
        /* No challenge outstanding, or this one already answered once --
         * single-use, so a captured attempt cannot be replayed against it.
         * Same status either way: the caller's correct response to both is
         * identical, and distinguishing them would just be two ways to find
         * out the same thing, badly. */
        return TFW_ERR_CRYPTO;
    }
    /* Consumed now, before the verify runs, whether or not it succeeds. */
    challenge_pending = 0u;

    tfw_pgm_copy(&msg[0], TFW_AUTH_DOMAIN, TFW_AUTH_DOMAIN_LEN);
    memcpy(&msg[TFW_AUTH_DOMAIN_LEN], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    memcpy(&msg[TFW_AUTH_DOMAIN_LEN + TFW_BOOT_SERIAL_LEN], nonce, TFW_AUTH_NONCE_LEN);
    (void)tfw_crypto_sha256(msg, (uint16_t)sizeof msg, digest);

    /* The nonce is spent whether the signature checks out or not -- it must
     * not be usable again either way. */
    memset(nonce, 0, sizeof nonce);

    tfw_platform_id_operator_pubkey(pubkey);

    /*
     * There is no software byte-for-byte secret comparison here to make
     * constant-time: the actual check is tfw_crypto_verify_operator, which on
     * this tier runs inside the element. Moving the comparison into hardware
     * the host never touches IS the constant-time answer for an asymmetric
     * scheme -- there is no shared secret in this process for a timing
     * side-channel to leak.
     */
    st = tfw_crypto_verify_operator(digest, sig, pubkey);
    if (st != TFW_OK) {
        return st;
    }
    authenticated = 1u;
    return TFW_OK;
}
