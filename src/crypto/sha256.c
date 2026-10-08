/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SHA-256, FIPS 180-4.
 *
 * Written out rather than pulled in because the alternatives are worse on this
 * part: a table-driven implementation costs flash we do not have, and a
 * general-purpose crypto library brings a build system and a portability layer
 * along with it. This is one file, has no dependencies, and is pinned to the
 * published NIST vectors -- which is the only property that matters for a hash.
 *
 * The round constants live in flash on AVR via TFW_PROGMEM; reading them
 * costs one instruction and saves 256 bytes of a 2 KB SRAM.
 */

#include <string.h>
#include "sha256.h"
#include "tfw/progmem.h"

static const uint32_t K[64] TFW_PROGMEM = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define KCONST(i) tfw_pgm_u32(&K[(i)])

#define ROR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ ((~(x)) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROR(x, 2)  ^ ROR(x, 13) ^ ROR(x, 22))
#define EP1(x)  (ROR(x, 6)  ^ ROR(x, 11) ^ ROR(x, 25))
#define SIG0(x) (ROR(x, 7)  ^ ROR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR(x, 17) ^ ROR(x, 19) ^ ((x) >> 10))

static void transform(tfw_sha256_t *c, const uint8_t *chunk)
{
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h;
    uint8_t  i;

    for (i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)chunk[i * 4u] << 24)
             | ((uint32_t)chunk[i * 4u + 1u] << 16)
             | ((uint32_t)chunk[i * 4u + 2u] << 8)
             |  (uint32_t)chunk[i * 4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        w[i] = SIG1(w[i - 2u]) + w[i - 7u] + SIG0(w[i - 15u]) + w[i - 16u];
    }

    a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
    e = c->state[4]; f = c->state[5]; g  = c->state[6]; h = c->state[7];

    for (i = 0u; i < 64u; i++) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + KCONST(i) + w[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g;  c->state[7] += h;
}

void tfw_sha256_init(tfw_sha256_t *c)
{
    c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
    c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
    c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
    c->bitlen = 0u;
    c->buflen = 0u;
}

void tfw_sha256_update(tfw_sha256_t *c, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; i++) {
        c->buf[c->buflen++] = data[i];
        if (c->buflen == 64u) {
            transform(c, c->buf);
            c->bitlen += 512u;
            c->buflen  = 0u;
        }
    }
}

void tfw_sha256_final(tfw_sha256_t *c, uint8_t out[32])
{
    uint8_t  i = c->buflen;
    uint64_t total;

    /* Pad: a 1 bit, then zeros, then the length. If the length will not fit in
     * this block, the padding spills into another -- the case an implementation
     * that only ever hashes short inputs silently gets wrong. */
    c->buf[i++] = 0x80u;
    if (i > 56u) {
        while (i < 64u) { c->buf[i++] = 0u; }
        transform(c, c->buf);
        i = 0u;
    }
    while (i < 56u) { c->buf[i++] = 0u; }

    total = c->bitlen + (uint64_t)c->buflen * 8u;
    for (i = 0u; i < 8u; i++) {
        c->buf[63u - i] = (uint8_t)(total >> (8u * i));
    }
    transform(c, c->buf);

    for (i = 0u; i < 8u; i++) {
        out[i * 4u]      = (uint8_t)(c->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(c->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(c->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(c->state[i]);
    }
}

void tfw_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    tfw_sha256_t c;
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, data, len);
    tfw_sha256_final(&c, out);
}

void tfw_hmac_sha256(const uint8_t *key, uint8_t keylen,
                    const uint8_t *data, uint16_t datalen,
                    uint8_t out[32])
{
    uint8_t     k[64];
    uint8_t     pad[64];
    uint8_t     inner[32];
    tfw_sha256_t c;
    uint8_t     i;

    memset(k, 0, sizeof k);
    if (keylen > 64u) {
        tfw_sha256(key, keylen, k);      /* an over-long key is hashed first */
    } else {
        memcpy(k, key, keylen);
    }

    for (i = 0u; i < 64u; i++) { pad[i] = (uint8_t)(k[i] ^ 0x36u); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, pad, 64u);
    tfw_sha256_update(&c, data, datalen);
    tfw_sha256_final(&c, inner);

    for (i = 0u; i < 64u; i++) { pad[i] = (uint8_t)(k[i] ^ 0x5cu); }
    tfw_sha256_init(&c);
    tfw_sha256_update(&c, pad, 64u);
    tfw_sha256_update(&c, inner, 32u);
    tfw_sha256_final(&c, out);

    /* The expanded key is derived from a secret. Leaving it on the stack for
     * whatever runs next to find would undo the point of holding the key in a
     * protected slot at all. */
    memset(k, 0, sizeof k);
    memset(pad, 0, sizeof pad);
    memset(inner, 0, sizeof inner);
}
