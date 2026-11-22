/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The secure-world DICE service (#590 slice b): docs/plans/590b.md.
 *
 * Built over the existing code without changing it: the DeviceID key is
 * src/crypto/deviceid_p256.c over HMAC-SHA256(UDS, TFW_DEVICEID_LABEL), the
 * seed tfw-boot derives; the alias certificate is src/caps/alias.c's
 * tfw_alias_tbs_digest and tfw_alias_cert. The alias key is P-256 over
 * HMAC-SHA256(CDI, TFW_DICE_ALIAS_LABEL), apart from the DeviceID key: every
 * entry point that signs uses the alias key only, so the non-secure world can
 * never make the DeviceID key sign anything (ruling 935).
 *
 * Every output is built in secure memory and copied to the caller only once
 * the whole answer is ready, so a refusal writes nothing. Non-secure inputs are
 * copied in before use, so the caller cannot change them mid-call.
 */

#include <string.h>
#include "dice_service.h"
#include "../crypto/sha256.h"
#include "../crypto/deviceid_p256.h"
#include "../caps/alias.h"
#include "../core/secure_zero.h"
#include "tfw/protocol.h"

static uint8_t           s_seed[32];
static uint8_t           s_cdi[32];
static uint8_t           s_ueid[TFW_ATTEST_ID_LEN];
static uint8_t           s_fw_hash[TFW_SHA256_LEN];
static tfw_crypto_tier_t s_tier;
static uint8_t           s_ready;
static uint8_t           s_alias[32];
static uint8_t           s_alias_ready;

/* The alias seed, or NULL while it is not derived: no caller falls back to
 * the DeviceID seed. */
static const uint8_t *alias_seed(void)
{
    return s_alias_ready ? s_alias : NULL;
}

tfw_status_t tfw_dice_service_init(const tfw_dice_inputs_t *in)
{
    uint8_t pub[TFW_DICE_KEY_LEN];

    tfw_dice_service_clear();
    if (in == NULL || in->uds == NULL || in->cdi == NULL || in->ueid == NULL ||
        in->fw_hash == NULL) {
        return TFW_ERR_RANGE;
    }
    tfw_hmac_sha256(in->uds, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    (uint16_t)TFW_DEVICEID_LABEL_LEN, s_seed);
    /* A seed that maps to no valid scalar has no key to serve. */
    if (!tfw_deviceid_p256_public(s_seed, pub)) {
        tfw_dice_service_clear();
        return TFW_ERR_CRYPTO;
    }
    memcpy(s_cdi, in->cdi, sizeof s_cdi);
    tfw_hmac_sha256(s_cdi, 32u, (const uint8_t *)TFW_DICE_ALIAS_LABEL,
                    (uint16_t)TFW_DICE_ALIAS_LABEL_LEN, s_alias);
    if (!tfw_deviceid_p256_public(s_alias, pub)) {
        tfw_dice_service_clear();
        return TFW_ERR_CRYPTO;
    }
    s_alias_ready = 1u;
    memcpy(s_ueid, in->ueid, sizeof s_ueid);
    memcpy(s_fw_hash, in->fw_hash, sizeof s_fw_hash);
    s_tier  = in->tier;
    s_ready = 1u;
    return TFW_OK;
}

void tfw_dice_service_clear(void)
{
    s_ready       = 0u;
    s_alias_ready = 0u;
    tfw_secure_zero(s_seed, sizeof s_seed);
    tfw_secure_zero(s_alias, sizeof s_alias);
    tfw_secure_zero(s_cdi, sizeof s_cdi);
    memset(s_ueid, 0, sizeof s_ueid);
    memset(s_fw_hash, 0, sizeof s_fw_hash);
    s_tier = TFW_CRYPTO_SOFTWARE;
}

static tfw_status_t put_key(const uint8_t *seed, uint8_t *out, uint32_t cap)
{
    uint8_t pub[TFW_DICE_KEY_LEN];

    if (!TFW_NS_CHECK(out, cap) || cap < TFW_DICE_KEY_LEN) {
        return TFW_ERR_RANGE;
    }
    if (!s_ready || seed == NULL) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (!tfw_deviceid_p256_public(seed, pub)) {
        return TFW_ERR_CRYPTO;
    }
    memcpy(out, pub, sizeof pub);
    return TFW_OK;
}

TFW_NSC tfw_status_t tfw_dice_deviceid_pub(uint8_t *out, uint32_t cap)
{
    return put_key(s_seed, out, cap);
}

TFW_NSC tfw_status_t tfw_dice_alias_pub(uint8_t *out, uint32_t cap)
{
    return put_key(alias_seed(), out, cap);
}

TFW_NSC tfw_status_t tfw_dice_alias_sign(const uint8_t *digest, uint32_t dlen,
                                         uint8_t *sig, uint32_t cap)
{
    uint8_t d[TFW_DICE_DIGEST_LEN];
    uint8_t s[TFW_DICE_SIG_LEN];
    const uint8_t *k;

    if (dlen != TFW_DICE_DIGEST_LEN || !TFW_NS_CHECK_READ(digest, dlen) ||
        !TFW_NS_CHECK(sig, cap) || cap < TFW_DICE_SIG_LEN) {
        return TFW_ERR_RANGE;
    }
    /* Alias key only: a digest the caller chose is never signed by the
     * DeviceID key, and nothing is signed before the alias key exists. */
    k = alias_seed();
    if (!s_ready || k == NULL) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    memcpy(d, digest, sizeof d);
    if (!tfw_deviceid_p256_sign(k, d, s)) {
        return TFW_ERR_CRYPTO;
    }
    memcpy(sig, s, sizeof s);
    return TFW_OK;
}

TFW_NSC tfw_status_t tfw_dice_alias_cert(tfw_dice_cert_page_t *req)
{
    tfw_dice_cert_page_t r;
    uint8_t           pub[TFW_DICE_KEY_LEN];
    uint8_t           sig[TFW_DICE_SIG_LEN];
    uint8_t           digest[TFW_SHA256_LEN];
    uint8_t           page[TFW_DICE_PAGE_MAX];
    uint16_t          t = 0u;
    uint8_t           g = 0u;
    tfw_alias_parts_t p;
    tfw_status_t      st;
    const uint8_t    *k;

    if (!TFW_NS_CHECK(req, sizeof *req)) {
        return TFW_ERR_RANGE;
    }
    /* Copied in once: the caller cannot move out or cap after the check. */
    memcpy(&r, req, sizeof r);
    if (!TFW_NS_CHECK(r.out, r.cap) || r.off > UINT16_MAX) {
        return TFW_ERR_RANGE;
    }
    k = alias_seed();
    if (!s_ready || k == NULL) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (!tfw_deviceid_p256_public(k, pub)) {
        return TFW_ERR_CRYPTO;
    }
    p.nonce   = r.nonce;
    p.ueid    = s_ueid;
    p.fw_hash = s_fw_hash;
    p.pubkey  = pub;
    p.sig     = sig;
    p.tier    = s_tier;
    /* The same two steps as alias.c's mint: the TBS digest, then its ES256
     * signature. The subject key is the alias key and the signer is the
     * DeviceID key (DICE layer 0 certifies layer 1), so the certificate
     * chains to tfw_dice_deviceid_pub. The TBS is built here from secure
     * state; the caller's nonce is one fixed field inside it, so the
     * DeviceID key never signs a digest the caller chose. */
    memset(sig, 0, sizeof sig);
    st = tfw_alias_tbs_digest(&p, digest);
    if (st != TFW_OK) {
        return st;
    }
    if (!tfw_deviceid_p256_sign(s_seed, digest, sig)) {
        return TFW_ERR_CRYPTO;
    }
    st = tfw_alias_cert(&p, (uint16_t)r.off, page,
                        (uint8_t)(r.cap < TFW_DICE_PAGE_MAX ? r.cap : TFW_DICE_PAGE_MAX),
                        &t, &g);
    if (st != TFW_OK) {
        return st;
    }
    memcpy(r.out, page, g);
    req->total = t;
    req->got   = g;
    return TFW_OK;
}
