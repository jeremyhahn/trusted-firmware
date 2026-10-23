/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The P-256 DeviceID derived from a UDS, and its ES256 signature.
 *
 * ONE derivation for every root whose UDS software can MAC under: the ESP32's
 * HMAC-peripheral eFuse key (src/hal/esp32/crypto_hmac_p256.c) and, once that
 * row signs with P-256, the AVR soft root (docs/SPEC.md row dice-attestation).
 * L1: a second root links this file; it does not grow its own. The design is
 * docs/multi-arch/esp32-p4-design.md section 2.1, option (a).
 *
 *   seed    HMAC-SHA256(UDS, "tfw-deviceid-v1"), computed by the root.
 *   scalar  the seed itself if uECC_compute_public_key accepts it; else
 *           SHA-256 of it, again until it does. That is the element model's
 *           reduction, the rule of the design's section 2.1
 *           (test/model/atecc608a.c). It accepts less than [1, n-1]: its
 *           co-Z ladder ends at the point at infinity for the scalars 1 and
 *           n-1, and EccPoint_compute_public_key refuses the zero point. A
 *           range compare comes first, so a seed outside [1, n-1] (a 2^-32
 *           event) costs no point multiplication. A refusal that survives
 *           DERIVE_TRIES hashes -- a blinding RNG that fails, not a scalar --
 *           is returned, and the key is not used.
 *   point   d.G, X || Y big-endian, 64 bytes: the wire's PUBKEY.
 *   sign    ECDSA P-256 with k from RFC 6979 section 3.2, HMAC-SHA256, so a
 *           signature needs no entropy. Not micro-ecc's
 *           uECC_sign_deterministic: its k is the RFC's HMAC-DRBG output read
 *           back as little-endian words, so it does not reproduce the RFC's
 *           signatures (test/unit/test_model_p256.c says the same of the
 *           model). Here k is bits2int(T) as the RFC defines it, handed to
 *           micro-ecc's uECC_sign_with_k, and RFC 6979 A.2.5's signatures are
 *           the test (test/unit/test_deviceid_p256.c).
 *
 * No heap and no static state. The scalar, the RFC's K and V, and the HMAC
 * message that carries the scalar live on the stack for one call and are
 * zeroed with tfw_secure_zero before it returns. This file's own frames on
 * the signing path are 304 bytes deep (tfw_deviceid_p256_sign 80 and the RFC
 * 6979 signer 224, gcc -Os -fstack-usage; the scalar's branch, 80 and derive
 * 48, is shallower); the stack it needs is that plus the
 * frames of the HMAC-SHA256 and micro-ecc calls beneath it, the deepest of
 * them micro-ecc's point multiplication.
 */
#include <string.h>
#include "uECC.h"
#include "sha256.h"
#include "../core/secure_zero.h"
#include "deviceid_p256.h"

/* micro-ecc v1.2 defines this, "for testing", without declaring it in uECC.h.
 * It is ECDSA with the caller's k, big-endian, which RFC 6979 needs. */
int uECC_sign_with_k(const uint8_t *private_key, const uint8_t *message_hash,
                     unsigned hash_size, const uint8_t *k, uint8_t *signature,
                     uECC_Curve curve);

/* The order n of the P-256 base point, big-endian (SEC 2, section 2.4.2). */
static const uint8_t P256_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51
};

/* Hashes of the scalar that micro-ecc may refuse before the derivation does,
 * and RFC 6979 rounds before a signature does. From the scalar or k alone each
 * further try is a 2^-32 event at most; a refusal past them is the blinding
 * RNG's. */
#define DERIVE_TRIES 8u
#define SIGN_ROUNDS  8u

/* v < n, for a big-endian 32-byte v. */
static int below_n(const uint8_t v[32])
{
    uint8_t i;

    for (i = 0u; i < 32u; i++) {
        if (v[i] != P256_N[i]) {
            return v[i] < P256_N[i];
        }
    }
    return 0;
}

static int is_zero(const uint8_t v[32])
{
    uint8_t acc = 0u;
    uint8_t i;

    for (i = 0u; i < 32u; i++) {
        acc = (uint8_t)(acc | v[i]);
    }
    return acc == 0u;
}

/* v -= n. One subtraction is a full reduction: a 256-bit v is below 2n. */
static void sub_n(uint8_t v[32])
{
    uint16_t borrow = 0u;
    uint8_t  i = 32u;

    while (i--) {
        uint16_t t = (uint16_t)((uint16_t)v[i] - (uint16_t)P256_N[i] - borrow);

        v[i]   = (uint8_t)t;
        borrow = (uint16_t)((t >> 8) & 1u);
    }
}

/* The scalar and its point, or 0 with both zeroed. */
static int derive(const uint8_t seed[32], uint8_t d[32], uint8_t pub[64])
{
    uint8_t tries;

    memcpy(d, seed, 32u);
    for (tries = 0u; tries < DERIVE_TRIES; tries++) {
        while (is_zero(d) || !below_n(d)) {
            tfw_sha256(d, 32u, d);
        }
        if (uECC_compute_public_key(d, pub, uECC_secp256r1())) {
            return 1;
        }
        tfw_sha256(d, 32u, d);
    }
    tfw_secure_zero(d, 32u);
    memset(pub, 0x00, 64u);
    return 0;
}

int tfw_deviceid_p256_scalar(const uint8_t seed[32], uint8_t d[32])
{
    uint8_t pub[64];        /* the public point: nothing to zero */

    return derive(seed, d, pub);
}

int tfw_deviceid_p256_public(const uint8_t seed[32], uint8_t pub[64])
{
    uint8_t d[32];
    int     ok = derive(seed, d, pub);

    tfw_secure_zero(d, sizeof d);
    return ok;
}

int tfw_p256_sign_rfc6979(const uint8_t d[32], const uint8_t digest[32],
                          uint8_t sig[64])
{
    /* V || sep || int2octets(x) || bits2octets(h1): the message of steps d and
     * f. V is its first 32 bytes, so step h's HMAC_K(V || 0x00) is its first 33. */
    uint8_t  m[97];
    uint8_t  k_key[32];
    uint8_t *v = m;
    uint8_t  round;
    int      ok = 0;

    if (is_zero(d) || !below_n(d)) {
        memset(sig, 0x00, 64u);
        return 0;
    }

    memset(v, 0x01, 32u);                             /* step b */
    memset(k_key, 0x00, sizeof k_key);                /* step c */
    memcpy(&m[33], d, 32u);
    memcpy(&m[65], digest, 32u);
    if (!below_n(&m[65])) {
        sub_n(&m[65]);                                /* bits2octets, 2.3.4 */
    }
    m[32] = 0x00u;
    tfw_hmac_sha256(k_key, 32u, m, 97u, k_key);       /* step d */
    tfw_hmac_sha256(k_key, 32u, v, 32u, v);           /* step e */
    m[32] = 0x01u;
    tfw_hmac_sha256(k_key, 32u, m, 97u, k_key);       /* step f */
    tfw_hmac_sha256(k_key, 32u, v, 32u, v);           /* step g */
    for (round = 0u; round < SIGN_ROUNDS; round++) {  /* step h */
        /* qlen = hlen = 256: T is one V, and k = bits2int(T) is V itself.
         * uECC_sign_with_k refuses a k outside [1, n-1] and an r of zero,
         * which are the RFC's cases for another round. */
        tfw_hmac_sha256(k_key, 32u, v, 32u, v);
        if (uECC_sign_with_k(d, digest, 32u, v, sig, uECC_secp256r1())) {
            ok = 1;
            break;
        }
        m[32] = 0x00u;
        tfw_hmac_sha256(k_key, 32u, m, 33u, k_key);
        tfw_hmac_sha256(k_key, 32u, v, 32u, v);
    }
    if (!ok) {
        memset(sig, 0x00, 64u);
    }
    tfw_secure_zero(m, sizeof m);
    tfw_secure_zero(k_key, sizeof k_key);
    return ok;
}

int tfw_deviceid_p256_sign(const uint8_t seed[32], const uint8_t digest[32],
                           uint8_t sig[64])
{
    uint8_t d[32];
    int     ok = tfw_deviceid_p256_scalar(seed, d)
                 && tfw_p256_sign_rfc6979(d, digest, sig);

    if (!ok) {
        memset(sig, 0x00, 64u);
    }
    tfw_secure_zero(d, sizeof d);
    return ok;
}
