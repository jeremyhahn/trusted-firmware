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
#include "ag/crypto.h"
#include "ag/boot.h"
#include "ag/protocol.h"

/* Domain-separates the signed message from anything else an operator key
 * might ever be asked to sign -- and from ATTEST's and ATTEST_CERT's own
 * nonce-bound messages, which this is not and must not be confusable with. */
static const char AG_AUTH_DOMAIN[] = "ag-auth-v1";
#define AG_AUTH_DOMAIN_LEN (sizeof(AG_AUTH_DOMAIN) - 1u)   /* drop the NUL */

static uint8_t nonce[AG_AUTH_NONCE_LEN];
static uint8_t challenge_pending;
static uint8_t authenticated;

void ag_auth_session_init(void)
{
    memset(nonce, 0, sizeof nonce);
    challenge_pending = 0u;
    authenticated     = 0u;
}

ag_status_t ag_auth_gate(void)
{
    return authenticated ? AG_OK : AG_ERR_AUTH_REQUIRED;
}

ag_status_t ag_auth_cmd_challenge(ag_reader_t *r, ag_writer_t *w)
{
    (void)r;

    /* Nonces from the provider's RNG -- the element's real TRNG on this
     * tier, never anything this file invents. AG_ERR_CRYPTO (not a nonce that
     * merely looks random) if the element will not answer. */
    if (ag_crypto_random(nonce, AG_AUTH_NONCE_LEN) != AG_OK) {
        challenge_pending = 0u;
        return AG_ERR_CRYPTO;
    }
    challenge_pending = 1u;

    /* A fresh challenge drops any session already granted. Asking for one is
     * the host declaring it wants to re-prove itself, and an old grant must
     * not survive that untouched. */
    authenticated = 0u;

    ag_write_blob(w, nonce, AG_AUTH_NONCE_LEN);
    return AG_OK;
}

ag_status_t ag_auth_cmd_response(ag_reader_t *r, ag_writer_t *w)
{
    const uint8_t *sig;
    uint8_t        msg[AG_AUTH_DOMAIN_LEN + AG_BOOT_SERIAL_LEN + AG_AUTH_NONCE_LEN];
    uint8_t        digest[AG_SHA256_LEN];
    uint8_t        pubkey[AG_BOOT_OPERATOR_PUBKEY_LEN];
    ag_status_t    st;

    (void)w;

    /* The dispatcher already enforced AG_AUTH_SIG_LEN exactly (commands.def's
     * min/max for this id), so no arity check belongs here -- see
     * src/core/commands.def's own header for why that check lives in
     * exactly one place. */
    sig = ag_read_blob(r, AG_AUTH_SIG_LEN);
    if (!sig) {
        return AG_ERR_BAD_ARGC;
    }

    if (!challenge_pending) {
        /* No challenge outstanding, or this one already answered once --
         * single-use, so a captured attempt cannot be replayed against it.
         * Same status either way: the caller's correct response to both is
         * identical, and distinguishing them would just be two ways to find
         * out the same thing, badly. */
        return AG_ERR_CRYPTO;
    }
    /* Consumed now, before the verify runs, whether or not it succeeds. */
    challenge_pending = 0u;

    memcpy(&msg[0], AG_AUTH_DOMAIN, AG_AUTH_DOMAIN_LEN);
    memcpy(&msg[AG_AUTH_DOMAIN_LEN], ag_platform_id_serial(), AG_BOOT_SERIAL_LEN);
    memcpy(&msg[AG_AUTH_DOMAIN_LEN + AG_BOOT_SERIAL_LEN], nonce, AG_AUTH_NONCE_LEN);
    (void)ag_crypto_sha256(msg, (uint16_t)sizeof msg, digest);

    /* The nonce is spent whether the signature checks out or not -- it must
     * not be usable again either way. */
    memset(nonce, 0, sizeof nonce);

    ag_platform_id_operator_pubkey(pubkey);

    /*
     * There is no software byte-for-byte secret comparison here to make
     * constant-time: the actual check is ag_crypto_verify_operator, which on
     * this tier runs inside the element. Moving the comparison into hardware
     * the host never touches IS the constant-time answer for an asymmetric
     * scheme -- there is no shared secret in this process for a timing
     * side-channel to leak.
     */
    st = ag_crypto_verify_operator(digest, sig, pubkey);
    if (st != AG_OK) {
        return st;
    }
    authenticated = 1u;
    return AG_OK;
}
