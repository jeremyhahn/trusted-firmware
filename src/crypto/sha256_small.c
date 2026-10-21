/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * SHA-256 and HMAC-SHA256, size first: the soft tfw-boot's (issue #130).
 *
 * FIPS 180-4 section 6.2.2, implemented exactly, and the same API as
 * sha256.c (sha256.h), which the application keeps for its speed. ONE of the
 * two is linked into an image, chosen by object list: mk/boot.mk's
 * BOOT_MEASURE=soft links this one. test/unit/test_sha256.c runs against both,
 * so they cannot disagree.
 *
 * Where sha256.c's bytes go on AVR, and what this file does instead:
 *
 *   - every ROR and SHR there is a constant 32-bit shift pair that avr-gcc
 *     expands inline, twelve rotates and two shifts per round and schedule
 *     step. Here one rotate-or-shift routine serves all four sigma functions,
 *     a byte step and then single bits.
 *   - the schedule is W[64] there, 256 bytes of stack and four indexed loads
 *     per word. Here it is the 16-word window of section 6.2.2 step 1, kept
 *     rolling: W[t] for t >= 16 replaces W[t - 16] in place.
 *   - the eight working variables are named there, so each round's shuffle is
 *     eight 32-bit moves. Here they are an array, moved by one loop.
 *   - the length there is a 64-bit variable shift, which on AVR is a libgcc
 *     call. Here the length is a 32-bit BYTE count, written as its two
 *     big-endian words n >> 29 and n << 3 by the store shared with the
 *     digest. Correct below 2^32 bytes; beyond, it wraps (tfw_sha256_final).
 *
 * The K table is in flash through tfw/progmem.h, as there. The cost is time:
 * a rotate is a loop, so a block takes several times as many cycles.
 */
#include <string.h>
#include "sha256.h"
#include "core/secure_zero.h"
#include "tfw/progmem.h"

static const uint32_t K[64] TFW_PROGMEM = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

/* ROTR^n(x) when rot is 1, SHR^n(x) when it is 0 (FIPS 180-4 section 3.2). */
static uint32_t rsh(uint32_t x, uint8_t n, uint8_t rot)
{
    while (n >= 8u) {
        x = (x >> 8) | (rot ? (x << 24) : 0u);
        n = (uint8_t)(n - 8u);
    }
    while (n != 0u) {
        x = (x >> 1) | (rot ? (x << 31) : 0u);
        n--;
    }
    return x;
}

/* The four sigma functions of section 4.1.2 have one shape: three terms, the
 * last a rotate for the upper-case pair and a shift for the lower-case. One
 * loop over the three, so rsh is expanded once. */
enum { SIG_EP0, SIG_EP1, SIG_S0, SIG_S1 };

static const uint8_t SIG_R[4][3] TFW_PROGMEM = {
    { 2u, 13u, 22u },   /* Sigma0, rotates   */
    { 6u, 11u, 25u },   /* Sigma1, rotates   */
    { 7u, 18u,  3u },   /* sigma0, last SHR  */
    { 17u, 19u, 10u }   /* sigma1, last SHR  */
};

static uint32_t sigma(uint32_t x, uint8_t f)
{
    uint32_t r = 0u;
    uint8_t  k;

    for (k = 0u; k < 3u; k++) {
        r ^= rsh(x, (uint8_t)tfw_pgm_far_u8(SIG_R, &SIG_R[f][k]),
                 (uint8_t)(k < 2u || f < SIG_S0));
    }
    return r;
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t x = 0u;
    uint8_t  i;

    for (i = 0u; i < 4u; i++) {
        x = (x << 8) | p[i];
    }
    return x;
}

/* Out of line: tfw_sha256_final's three calls, inlined, cost more (#173). */
static void put32(uint8_t *p, uint32_t x) __attribute__((noinline));
static void put32(uint8_t *p, uint32_t x)
{
    uint8_t i;

    for (i = 4u; i != 0u; i--) {
        p[i - 1u] = (uint8_t)x;
        x >>= 8;
    }
}

static void transform(tfw_sha256_t *c, const uint8_t *chunk)
{
    /* The working variables and the schedule in one struct: one clear covers
     * both, and v comes first so its fixed-index words sit within the AVR's
     * Y+q reach (q <= 63); w is only ever indexed by a variable. */
    struct {
        uint32_t v[8];
        uint32_t w[16];
    } s;
    uint8_t t;

    memcpy(s.v, c->state, sizeof s.v);

    for (t = 0u; t < 64u; t++) {
        uint32_t t1, t2;

        if (t < 16u) {
            s.w[t] = get32(&chunk[t * 4u]);
        } else {
            /* W[t] = s1(W[t-2]) + W[t-7] + s0(W[t-15]) + W[t-16], into the
             * slot W[t-16] held. */
            s.w[t & 15u] += sigma(s.w[(t - 2u) & 15u], SIG_S1)
                          + s.w[(t - 7u) & 15u]
                          + sigma(s.w[(t - 15u) & 15u], SIG_S0);
        }
        /* v = a b c d e f g h. Ch and Maj in their fewest-operation forms,
         * equal bit for bit to section 4.1.2's: Ch = g ^ (e & (f ^ g)),
         * Maj = (a & b) | (c & (a | b)). */
        t1 = s.v[7] + sigma(s.v[4], SIG_EP1) + (s.v[6] ^ (s.v[4] & (s.v[5] ^ s.v[6])))
           + tfw_pgm_far_u32(K, &K[t]) + s.w[t & 15u];
        t2 = sigma(s.v[0], SIG_EP0) + ((s.v[0] & s.v[1]) | (s.v[2] & (s.v[0] | s.v[1])));
        memmove(&s.v[1], &s.v[0], 7u * sizeof s.v[0]);
        s.v[4] += t1;
        s.v[0]  = t1 + t2;
    }

    for (t = 0u; t < 8u; t++) {
        c->state[t] += s.v[t];
    }

    /* The schedule and the working variables run backwards: from the last
     * sixteen words of w and the eight of v, the rounds undo to the state this
     * block started from, and that plus v is the digest. In an HMAC over the
     * UDS that digest is the CDI (TCG DICE HW Req 6.4), so neither is left in
     * this frame for the code that runs next. One call for both: each call
     * site costs tfw-boot flash, and the block count costs it none. */
    tfw_secure_zero(&s, sizeof s);
}

void tfw_sha256_init(tfw_sha256_t *c)
{
    static const uint32_t H0[8] TFW_PROGMEM = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint8_t i;

    for (i = 0u; i < 8u; i++) {
        c->state[i] = tfw_pgm_far_u32(H0, &H0[i]);
    }
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
            /* BYTES of whole blocks, in 32 bits: see tfw_sha256_final. */
            c->bitlen = (uint32_t)c->bitlen + 64u;
            c->buflen = 0u;
        }
    }
}

void tfw_sha256_final(tfw_sha256_t *c, uint8_t out[32])
{
    /*
     * THE LENGTH, WITH NO 64-BIT ARITHMETIC. In this object bitlen holds the
     * BYTE count of whole blocks in its low 32 bits, so the message length in
     * bits is n * 8 with n below 2^32: its high word is n >> 29 and its low
     * word n << 3, which is section 5.1.1's 64-bit big-endian length exactly.
     * libgcc's 64-bit shift helpers are never called.
     *
     * The range is every message below 2^32 bytes (4 GiB). Beyond it the
     * count wraps and the digest is wrong; it is not refused. No caller can
     * reach it: this object is the boot's, which hashes its own part's flash
     * (at most 256 KB) and HMAC inputs under 64 KB. sha256.c, which the
     * application links, keeps the full 2^64-bit range.
     */
    uint32_t n = (uint32_t)c->bitlen + c->buflen;
    uint8_t  i = c->buflen;

    /* Section 5.1.1: a 1 bit, zeros, then the 64-bit length, spilling into a
     * second block when the length does not fit in this one. One loop writes
     * the zeros of both blocks (#173). */
    c->buf[i++] = 0x80u;
    while (i != 56u) {
        if (i == 64u) {
            transform(c, c->buf);
            i = 0u;
        }
        c->buf[i++] = 0u;
    }
    put32(&c->buf[56], n >> 29);
    put32(&c->buf[60], n << 3);
    transform(c, c->buf);

    for (i = 0u; i < 8u; i++) {
        put32(&out[i * 4u], c->state[i]);
    }
}

void tfw_sha256(const uint8_t *data, uint16_t len, uint8_t out[32])
{
    tfw_sha256_t c;

    tfw_sha256_init(&c);
    tfw_sha256_update(&c, data, len);
    tfw_sha256_final(&c, out);
}

/* RFC 2104 over this hash, as sha256.c's is: same key handling, same result. */
void tfw_hmac_sha256(const uint8_t *key, uint8_t keylen,
                    const uint8_t *data, uint16_t datalen,
                    uint8_t out[32])
{
    /* Every secret this function holds, in one struct so that one clear
     * covers them all: each tfw_secure_zero call site costs tfw-boot flash. */
    struct {
        tfw_sha256_t c;
        uint8_t      inner[32];
        uint8_t      k[64];
    } s;
    uint8_t pass, i;

    memset(s.k, 0, sizeof s.k);
    if (keylen > 64u) {
        tfw_sha256(key, keylen, s.k);    /* an over-long key is hashed first */
    } else {
        memcpy(s.k, key, keylen);
    }

    /* Two passes of one loop: inner with ipad over the data, outer with opad
     * over the inner digest. The pad is the key XORed in place: ipad first,
     * then ipad ^ opad, which leaves the key XOR opad (#173). */
    for (pass = 0u; pass < 2u; pass++) {
        for (i = 0u; i < 64u; i++) {
            s.k[i] = (uint8_t)(s.k[i] ^ (pass ? (0x5cu ^ 0x36u) : 0x36u));
        }
        tfw_sha256_init(&s.c);
        tfw_sha256_update(&s.c, s.k, 64u);
        if (pass) {
            tfw_sha256_update(&s.c, s.inner, 32u);
        } else {
            tfw_sha256_update(&s.c, data, datalen);
        }
        tfw_sha256_final(&s.c, pass ? out : s.inner);
    }

    /* The expanded key is derived from a secret: not left on the stack. Nor
     * is the context, whose state is the result (the CDI, when the key is the
     * UDS) and whose buffer holds the inner digest. tfw_secure_zero, because
     * a memset of a local that is never read again is a dead store. */
    tfw_secure_zero(&s, sizeof s);
}
