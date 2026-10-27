/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The policy sets the QEMU HSM cases load (qemu_test.sh, docs/hsm-self-defence/
 * m7-design.md slice 3), signed by the build's SUIT emitter (tools/suit-emit/)
 * as the engine's unit test signs its own.
 *
 *   hsm_sets <dir>
 *
 * writes, into <dir>, integrated envelopes for mps2-an505's policy class:
 *
 *   hsm-long.cose    one policy, S5: [record], wipe; heartbeat and W 60 s;
 *                    sequence 2
 *   hsm-short.cose   the same, heartbeat 300 ms
 *   hsm-replay.cose  hsm-long's set under sequence 1, below the floor it sets
 *   hsm-forged.cose  hsm-long, signed by a key the board does not hold
 *
 * The tenant key is testdata/suit_vectors.h's throwaway one, private scalar
 * 0x40..0x5F, whose public half the board's port answers
 * (src/hal/cortex-m/mps2/hsm_port.c). The ids are literals, not the build's:
 * a class id read back from the profile would agree with whatever the profile
 * got wrong.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "emit.h"
#include "sha256.h"
#include "uECC.h"

/* f5ce0048-68e5-5fcf-b79b-5b1cffbf22ac, the project's vendor id. */
static const uint8_t VENDOR[16] = {
    0xf5, 0xce, 0x00, 0x48, 0x68, 0xe5, 0x5f, 0xcf,
    0xb7, 0x9b, 0x5b, 0x1c, 0xff, 0xbf, 0x22, 0xac,
};
/* b46b3e2f-0849-56df-9556-e94d0d85c91f, UUIDv5(vendor, "mps2-an505/policy"). */
static const uint8_t POLICY_CLASS[16] = {
    0xb4, 0x6b, 0x3e, 0x2f, 0x08, 0x49, 0x56, 0xdf,
    0x95, 0x56, 0xe9, 0x4d, 0x0d, 0x85, 0xc9, 0x1f,
};

/* {1: 1, 2: [[[5, [6], true]]], 3: {}, 4: heartbeat, 5: 60000} */
static const uint8_t SET_LONG[] = {
    0xa5, 0x01, 0x01, 0x02, 0x81, 0x81, 0x83, 0x05, 0x81, 0x06, 0xf5,
    0x03, 0xa0, 0x04, 0x19, 0xea, 0x60, 0x05, 0x19, 0xea, 0x60,
};
static const uint8_t SET_SHORT[] = {
    0xa5, 0x01, 0x01, 0x02, 0x81, 0x81, 0x83, 0x05, 0x81, 0x06, 0xf5,
    0x03, 0xa0, 0x04, 0x19, 0x01, 0x2c, 0x05, 0x19, 0xea, 0x60,
};

static const char URI[] = "#policy";

static uint32_t rng_state = 0x5017C0DEu;

/* ECDSA's per-signature nonce. A test key signing test sets needs no entropy,
 * only a nonce that is never reused across messages, which the xorshift's
 * stream gives within one run. */
static int nonce_rng(uint8_t *d, unsigned n)
{
    for (unsigned i = 0u; i < n; i++) {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        d[i] = (uint8_t)rng_state;
    }
    return 1;
}

static int write_envelope(const char *dir, const char *name, const uint8_t *body, size_t bn,
                          uint32_t seq, uint8_t key_base)
{
    uint8_t priv[32];
    uint8_t man[160];
    uint8_t dig[32];
    uint8_t tbs[192];
    uint8_t hash[32];
    uint8_t sig[64];
    uint8_t env[512];
    char    path[1024];
    size_t  mn;
    size_t  tn;
    size_t  n;
    FILE   *f;

    for (unsigned i = 0u; i < sizeof priv; i++) {
        priv[i] = (uint8_t)(key_base + i);
    }
    tfw_sha256(body, (uint16_t)bn, dig);
    mn = tfw_suit_emit_manifest_integrated(man, sizeof man, dig, (uint32_t)bn, seq, VENDOR,
                                           POLICY_CLASS, URI);
    tn = (mn != 0u) ? tfw_suit_emit_tbs(tbs, sizeof tbs, man, mn) : 0u;
    if (tn == 0u) {
        fprintf(stderr, "hsm_sets: %s: the manifest did not emit\n", name);
        return 1;
    }
    tfw_sha256(tbs, (uint16_t)tn, hash);
    if (uECC_sign(priv, hash, sizeof hash, sig, uECC_secp256r1()) != 1) {
        fprintf(stderr, "hsm_sets: %s: the signature failed\n", name);
        return 1;
    }
    n = tfw_suit_emit_envelope_integrated(env, sizeof env, man, mn, sig, URI, body, bn);
    if (n == 0u) {
        fprintf(stderr, "hsm_sets: %s: the envelope did not emit\n", name);
        return 1;
    }
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) {
        fprintf(stderr, "hsm_sets: %s: the path is too long\n", name);
        return 1;
    }
    f = fopen(path, "wb");
    if (f == NULL || fwrite(env, 1u, n, f) != n || fclose(f) != 0) {
        fprintf(stderr, "hsm_sets: %s: cannot write\n", path);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int bad = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: hsm_sets <dir>\n");
        return 2;
    }
    uECC_set_rng(nonce_rng);
    bad |= write_envelope(argv[1], "hsm-long.cose", SET_LONG, sizeof SET_LONG, 2u, 0x40u);
    bad |= write_envelope(argv[1], "hsm-short.cose", SET_SHORT, sizeof SET_SHORT, 2u, 0x40u);
    bad |= write_envelope(argv[1], "hsm-replay.cose", SET_LONG, sizeof SET_LONG, 1u, 0x40u);
    bad |= write_envelope(argv[1], "hsm-forged.cose", SET_LONG, sizeof SET_LONG, 2u, 0x60u);
    if (bad == 0) {
        printf("hsm_sets: wrote the HSM envelopes to %s\n", argv[1]);
    }
    return bad;
}
