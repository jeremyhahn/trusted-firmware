/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A seeded mutation test for the SUIT envelope reader (issue #78). The
 * envelope is staged with an OTA image, over TCP or serial: every byte of it
 * is the sender's until the signature says otherwise, and the CBOR walk that
 * finds the signature runs before it.
 *
 * The inputs are the literal envelopes, mutated -- bit flips, truncation, a
 * byte replaced by a CBOR head that claims a 1, 2, 4 or 8-byte length, a
 * length argument set to 0, its maximum and past it, appended bytes -- plus
 * pure random buffers. Each is read from a heap buffer of exactly the length
 * passed, so under `make test-asan` a read past the envelope is a fault, not
 * a pass.
 *
 * A read that succeeds must have read one of the two signed manifests: the
 * signature covers the manifest, so any success that differs from both is a
 * reader that judged one span and parsed another.
 *
 * The seed and the counts are literals: a failure reproduces bit for bit.
 */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "suit_vectors.h"

#define ITERATIONS 4000u
#define INPUT_CAP  (TFW_SUIT_ENVELOPE_MAX + 32u)

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static uint32_t            rng;
static tfw_suit_manifest_t signed_a, signed_b;

void setUp(void)
{
    rng = 0x5017C0DEu;
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, SUIT_PUB);
}

void tearDown(void) {}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

typedef struct {
    const uint8_t *bytes;
    uint16_t       len;
} seed_t;

static const seed_t seeds[] = {
    { SUIT_ENVELOPE,            (uint16_t)sizeof SUIT_ENVELOPE },
    { SUIT_ENVELOPE_CLASS_328P, (uint16_t)sizeof SUIT_ENVELOPE_CLASS_328P },
    { SUIT_ENVELOPE_UNKNOWN,    (uint16_t)sizeof SUIT_ENVELOPE_UNKNOWN },
    { SUIT_MANIFEST,            (uint16_t)sizeof SUIT_MANIFEST },
};
#define SEED_COUNT ((uint32_t)(sizeof seeds / sizeof seeds[0]))

/* CBOR heads whose argument follows in 1, 2, 4 or 8 bytes, for a byte string,
 * an array and a map, and the argument values at the edges. */
static const uint8_t heads[] = {
    0x58u, 0x59u, 0x5Au, 0x5Bu, 0x98u, 0x99u, 0x9Au, 0x9Bu, 0xB8u, 0xB9u, 0xBAu, 0xBBu,
    0x5Fu, 0x9Fu, 0xBFu, 0xFFu, 0xD8u, 0xD2u,
};
static const uint8_t args[] = { 0x00u, 0x01u, 0x7Fu, 0x80u, 0xFEu, 0xFFu };

static uint16_t make_input(uint8_t *b)
{
    const seed_t *s   = &seeds[rnd() % SEED_COUNT];
    uint16_t      n   = s->len;
    uint32_t      ops = 1u + rnd() % 3u;

    if (rnd() % 8u == 0u) {
        n = (uint16_t)(1u + rnd() % INPUT_CAP);
        for (uint16_t i = 0; i < n; i++) {
            b[i] = (uint8_t)rnd();
        }
        return n;
    }
    memcpy(b, s->bytes, n);
    while (ops-- > 0u) {
        uint16_t at = (uint16_t)(rnd() % n);

        switch (rnd() % 7u) {
        case 0:
            for (uint32_t k = 1u + rnd() % 8u; k > 0u; k--) {
                b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            break;
        case 1:
            n = (uint16_t)(1u + rnd() % n);
            break;
        case 2:
            b[at] = heads[rnd() % sizeof heads];
            break;
        case 3:
            /* A length argument: the byte after a head, set to an edge. */
            b[at] = args[rnd() % sizeof args];
            break;
        case 4:
            for (uint32_t k = 1u + rnd() % 32u; k > 0u && n < INPUT_CAP; k--) {
                b[n++] = (uint8_t)rnd();
            }
            break;
        case 5:
            /* A short head's inline length: 0, 23 (the largest), 24 (the next
             * byte holds it), keeping the major type. */
            b[at] = (uint8_t)((b[at] & 0xE0u) | ((rnd() % 3u == 0u) ? 0u : (rnd() % 2u) ? 23u : 24u));
            break;
        default:
            b[at] = (uint8_t)rnd();
            break;
        }
    }
    return n;
}

static int same(const tfw_suit_manifest_t *a, const tfw_suit_manifest_t *b)
{
    return a->seq == b->seq && a->image_size == b->image_size &&
           memcmp(a->vendor, b->vendor, sizeof a->vendor) == 0 &&
           memcmp(a->class_id, b->class_id, sizeof a->class_id) == 0 &&
           memcmp(a->image_digest, b->image_digest, sizeof a->image_digest) == 0;
}

static void test_mutated_envelopes_read_inside_their_contract(void)
{
    static uint8_t scratch[INPUT_CAP];
    unsigned long  ok = 0u, refused = 0u, crypto = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, &signed_a));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE_CLASS_328P,
                                            sizeof SUIT_ENVELOPE_CLASS_328P, &signed_b));

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        uint16_t            n  = make_input(scratch);
        uint8_t            *in = malloc(n);
        tfw_suit_manifest_t m;
        tfw_status_t        st;

        TEST_ASSERT_NOT_NULL(in);
        memcpy(in, scratch, n);
        st = tfw_suit_read(in, n, &m);
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_RANGE ||
                         st == TFW_ERR_UNSUPPORTED || st == TFW_ERR_CRYPTO);
        if (st == TFW_OK) {
            ok++;
            TEST_ASSERT_TRUE(n <= TFW_SUIT_ENVELOPE_MAX);
            TEST_ASSERT_TRUE(same(&m, &signed_a) || same(&m, &signed_b));
        } else if (st == TFW_ERR_CRYPTO) {
            crypto++;
        } else {
            refused++;
        }
        free(in);
    }

    /* Proof the loop ran: some inputs were refused by the walk, and some got
     * past it to the digest and signature, the reader's last checks. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, crypto);
    (void)ok;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_envelopes_read_inside_their_contract);
    return UNITY_END();
}
