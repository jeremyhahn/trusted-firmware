/*
 * test_boot_trailer_robust.c -- the image trailer gate, the anti-rollback floor
 * and the handoff record, fed hostile input from a fixed seed (row secure-boot,
 * #163).
 *
 * test_boot_trailer_mutate.c checks that a mutated trailer parses inside the
 * parser's contract. This suite checks the gate the parse feeds: every single-
 * bit flip of a valid trailer over a real image, random trailers with lengths at
 * and past the slot's edge, and long walks of the floor. The PRNG is the test's
 * own and its seed is a literal, so every run is the same run.
 *
 * gate() is tfw_boot_evaluate_at's order (boot/boot_policy.c) up to its one
 * call to the signature primitive: parse, board binding, rollback, measure,
 * compare. boot_policy.c is not in BOOT_LOGIC_SRCS, so the last step is taken
 * as what it is -- a function of the signed span and the signature -- and a
 * flip that reaches it must have changed one of the two.
 */

#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"

#define SEED        0x5EED0164uL
#define IMG_LEN     700u
#define VERSION     0x00000164uL
#define BOARD       0x5Au
#define RANDOM_ITER 6000u
#define FLOOR_WALK  20000u

static uint32_t rng;

/* xorshift32: small, and the same sequence on every host. */
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

typedef enum {
    AT_PARSE = 0,
    AT_BINDING,
    AT_FLOOR,
    AT_MEASURE,
    AT_SIGNATURE        /* passed every check this link has; the signature decides */
} stage_t;

static uint8_t       good[TFW_BOOT_TRAILER_LEN];
static uint32_t      measuring;     /* the length the gate handed the measure */
static unsigned long reads;

/* The reader the measure is given. Every read is inside the slot and inside
 * the length the parse admitted: a length is never used before its check. */
static tfw_status_t bounded(uint32_t addr, uint8_t *buf, uint8_t len)
{
    TEST_ASSERT_TRUE(measuring > 0u && measuring <= TFW_BOOT_MAX_IMAGE_LEN);
    /* An address below the base wraps to past the length, and fails too. */
    TEST_ASSERT_TRUE(addr - TFW_BOOT_APP_BASE < measuring);
    TEST_ASSERT_TRUE(len <= measuring - (addr - TFW_BOOT_APP_BASE));
    TEST_ASSERT_TRUE(addr + len <= TFW_BOOT_TRAILER_ADDR);
    tfw_boot_flash_read(addr, buf, len);
    reads++;
    return TFW_OK;
}

static stage_t gate(const uint8_t *raw, uint32_t floor, tfw_boot_trailer_t *tr,
                    tfw_status_t *st)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN];

    measuring = 0u;
    *st = tfw_boot_trailer_parse(raw, tr);
    if (*st != TFW_OK) {
        return AT_PARSE;
    }
    *st = tfw_boot_binding_permit(tr, BOARD);
    if (*st != TFW_OK) {
        return AT_BINDING;
    }
    *st = tfw_boot_rollback_permit(tr->image_version, floor);
    if (*st != TFW_OK) {
        return AT_FLOOR;
    }
    measuring = tr->image_len;
    *st = tfw_boot_measure_from(bounded, TFW_BOOT_APP_BASE, tr->image_len, m);
    measuring = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, *st);
    if (memcmp(m, tr->measurement, TFW_BOOT_MEASURE_LEN) != 0) {
        *st = TFW_ERR_CRYPTO;
        return AT_MEASURE;
    }
    return AT_SIGNATURE;
}

/* The verify's inputs: the signed span as it sits in flash, and the signature
 * as parsed. A trailer that reaches the signature with both the same as the
 * signed original's would be accepted by any backend. */
static int same_tuple(const uint8_t *raw, const tfw_boot_trailer_t *tr)
{
    return memcmp(raw, good, TFW_BOOT_TR_SIGNED_LEN) == 0
           && memcmp(tr->signature, &good[TFW_BOOT_TR_SIGNATURE], TFW_BOOT_SIG_LEN) == 0;
}

/* A seeded image at the application base, and a trailer that names it. */
static void install(void)
{
    uint8_t           *flash = tfw_boot_mock_flash();
    tfw_boot_trailer_t tr;

    TEST_ASSERT_TRUE(TFW_BOOT_APP_BASE + IMG_LEN <= tfw_boot_mock_flash_size());
    TEST_ASSERT_TRUE(IMG_LEN <= TFW_BOOT_MAX_IMAGE_LEN);
    for (uint32_t i = 0u; i < IMG_LEN; i++) {
        flash[TFW_BOOT_APP_BASE + i] = (uint8_t)rnd();
    }

    memset(&tr, 0, sizeof tr);
    tr.image_version = VERSION;
    tr.image_len     = IMG_LEN;
    tr.board_id      = BOARD;
    tr.image_flags   = 0u;
    measuring = IMG_LEN;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure_from(bounded, TFW_BOOT_APP_BASE, IMG_LEN,
                                                    tr.measurement));
    measuring = 0u;
    for (uint32_t i = 0u; i < TFW_BOOT_SIG_LEN; i++) {
        tr.signature[i] = (uint8_t)rnd();
    }
    tfw_boot_trailer_serialise(&tr, good);
}

void setUp(void)
{
    rng = SEED;
    reads = 0u;
    tfw_boot_mock_reset();
    install();
}

void tearDown(void) {}

/* The original passes every check up to the signature, with its own tuple. */
static void test_the_valid_trailer_reaches_the_signature(void)
{
    tfw_boot_trailer_t tr;
    tfw_status_t       st;

    TEST_ASSERT_EQUAL(AT_SIGNATURE, gate(good, VERSION, &tr, &st));
    TEST_ASSERT_TRUE(same_tuple(good, &tr));
    TEST_ASSERT_GREATER_THAN_UINT32(0u, reads);
}

/* All 1024 single-bit flips. Each is refused by the check that owns its field,
 * or reaches the signature with a changed signed span or signature -- except
 * the 128 in the reserved tail, which change nothing the gate reads. */
static void test_every_single_bit_flip_is_refused_or_changes_the_signed_tuple(void)
{
    unsigned long to_signature = 0u, expected_to_signature = 0u, inert = 0u;

    for (uint32_t bit = 0u; bit < 8u * TFW_BOOT_TRAILER_LEN; bit++) {
        uint8_t           *raw  = malloc(TFW_BOOT_TRAILER_LEN);
        uint32_t           byte = bit / 8u;
        uint8_t            mask = (uint8_t)(1u << (bit % 8u));
        tfw_boot_trailer_t tr;
        tfw_status_t       st;
        stage_t            at;

        TEST_ASSERT_NOT_NULL(raw);
        memcpy(raw, good, TFW_BOOT_TRAILER_LEN);
        raw[byte] ^= mask;
        reads = 0u;
        at = gate(raw, VERSION, &tr, &st);

        if (byte < TFW_BOOT_TR_BOARD_ID) {
            TEST_ASSERT_EQUAL(AT_PARSE, at);                 /* magic, layout */
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, st);
        } else if (byte == TFW_BOOT_TR_BOARD_ID) {
            TEST_ASSERT_EQUAL(AT_BINDING, at);
        } else if (byte == TFW_BOOT_TR_IMAGE_FLAGS) {
            if ((mask & TFW_BOOT_IMAGE_FLAGS_KNOWN) != 0u) {
                TEST_ASSERT_EQUAL(AT_SIGNATURE, at);
                expected_to_signature++;
            } else {
                TEST_ASSERT_EQUAL(AT_BINDING, at);
            }
        } else if (byte < TFW_BOOT_TR_IMAGE_LEN) {           /* image version */
            uint32_t v = VERSION ^ ((uint32_t)mask << (8u * (byte - TFW_BOOT_TR_IMAGE_VERSION)));
            if (v < VERSION) {
                TEST_ASSERT_EQUAL(AT_FLOOR, at);
                TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, st);
            } else {
                TEST_ASSERT_EQUAL(AT_SIGNATURE, at);
                expected_to_signature++;
            }
        } else if (byte < TFW_BOOT_TR_MEASUREMENT) {         /* image length */
            TEST_ASSERT_TRUE(at == AT_PARSE || at == AT_MEASURE);
            if (at == AT_PARSE) {
                TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
            }
        } else if (byte < TFW_BOOT_TR_SIGNATURE) {           /* measurement */
            TEST_ASSERT_EQUAL(AT_MEASURE, at);
            TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, st);
        } else if (byte < TFW_BOOT_TR_SIGNATURE + TFW_BOOT_SIG_LEN) { /* signature */
            TEST_ASSERT_EQUAL(AT_SIGNATURE, at);
            expected_to_signature++;
        } else {                                             /* reserved */
            /* Bytes [112, 128) are neither signed nor checked (include/tfw/
             * boot.h: "reserved"), so a flip there passes with the original's
             * tuple. What is asserted is that they are inert: the trailer the
             * gate parses is the original's, field for field. */
            tfw_boot_trailer_t orig;

            TEST_ASSERT_EQUAL(AT_SIGNATURE, at);
            TEST_ASSERT_TRUE(same_tuple(raw, &tr));
            TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trailer_parse(good, &orig));
            TEST_ASSERT_EQUAL_UINT32(orig.image_version, tr.image_version);
            TEST_ASSERT_EQUAL_UINT32(orig.image_len, tr.image_len);
            TEST_ASSERT_EQUAL_UINT8(orig.board_id, tr.board_id);
            TEST_ASSERT_EQUAL_UINT8(orig.image_flags, tr.image_flags);
            TEST_ASSERT_EQUAL_MEMORY(orig.measurement, tr.measurement, TFW_BOOT_MEASURE_LEN);
            TEST_ASSERT_EQUAL_MEMORY(orig.signature, tr.signature, TFW_BOOT_SIG_LEN);
            inert++;
            free(raw);
            continue;
        }

        /* Nothing was read for a trailer refused before the measure. */
        if (at < AT_MEASURE) {
            TEST_ASSERT_EQUAL_UINT32(0u, reads);
        }
        if (at == AT_SIGNATURE) {
            TEST_ASSERT_FALSE(same_tuple(raw, &tr));
            TEST_ASSERT_TRUE(tr.image_version >= VERSION);
            to_signature++;
        }
        free(raw);
    }
    TEST_ASSERT_EQUAL_UINT32(expected_to_signature, to_signature);
    TEST_ASSERT_EQUAL_UINT32(8u * (TFW_BOOT_TRAILER_LEN - TFW_BOOT_TR_SIGNATURE
                                   - TFW_BOOT_SIG_LEN), inert);
}

/* Random trailers, and the valid one with its length or version at and past
 * the slot's edge, against random floors. */
static void test_random_trailers_and_slot_edges_stay_in_the_slot(void)
{
    const uint32_t lens[] = {
        0u, 1u, IMG_LEN - 1u, IMG_LEN + 1u,
        TFW_BOOT_MAX_IMAGE_LEN - 1u, TFW_BOOT_MAX_IMAGE_LEN,
        TFW_BOOT_MAX_IMAGE_LEN + 1u, TFW_BOOT_MAX_IMAGE_LEN + TFW_BOOT_TRAILER_LEN,
        TFW_BOOT_APP_SIZE, TFW_BOOT_APP_SIZE + 1u, TFW_BOOT_APP_END,
        0x7FFFFFFFuL, 0x80000000uL, 0xFFFFFFFFuL,
    };
    const uint32_t vers[] = { 0u, 1u, VERSION - 1u, VERSION, VERSION + 1u, 0xFFFFFFFFuL };
    unsigned long  passed = 0u, refused = 0u;

    for (uint32_t it = 0u; it < RANDOM_ITER; it++) {
        uint8_t           *raw   = malloc(TFW_BOOT_TRAILER_LEN);
        uint32_t           floor = (rnd() % 2u) ? VERSION + (rnd() % 3u) - 1u : rnd();
        tfw_boot_trailer_t tr;
        tfw_status_t       st;
        stage_t            at;

        TEST_ASSERT_NOT_NULL(raw);
        memcpy(raw, good, TFW_BOOT_TRAILER_LEN);
        switch (rnd() % 4u) {
        case 0:
            for (uint32_t i = 0u; i < TFW_BOOT_TRAILER_LEN; i++) {
                raw[i] = (uint8_t)rnd();
            }
            break;
        case 1:
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_LEN], lens[rnd() % (sizeof lens / sizeof lens[0])]);
            break;
        case 2:
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_LEN], TFW_BOOT_MAX_IMAGE_LEN + 1u + rnd() % 0x10000u);
            break;
        default:
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION], vers[rnd() % (sizeof vers / sizeof vers[0])]);
            break;
        }

        reads = 0u;
        at = gate(raw, floor, &tr, &st);
        if (at > AT_PARSE) {
            TEST_ASSERT_TRUE(tr.image_len > 0u && tr.image_len <= TFW_BOOT_MAX_IMAGE_LEN);
        }
        if (at < AT_MEASURE) {
            TEST_ASSERT_EQUAL_UINT32(0u, reads);
        }
        if (at == AT_SIGNATURE) {
            TEST_ASSERT_TRUE(tr.image_version >= floor);
            TEST_ASSERT_EQUAL_UINT8(BOARD, tr.board_id);
            TEST_ASSERT_EQUAL_UINT8(0u, tr.image_flags & (uint8_t)~TFW_BOOT_IMAGE_FLAGS_KNOWN);
            passed++;
        } else {
            refused++;
        }
        free(raw);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, passed);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
}

/* A long walk of the floor under random, edge and parsed versions: it never
 * descends, never moves by more than its step, and moves only to the version
 * offered. A refused step leaves the caller's value as it was. */
static void test_the_floor_never_descends(void)
{
    uint32_t      floor = 0u;
    unsigned long raised = 0u, held = 0u, range = 0u;

    for (uint32_t it = 0u; it < FLOOR_WALK; it++) {
        uint32_t     v, out = 0xA5A5A5A5uL;
        tfw_status_t st;

        switch (rnd() % 6u) {
        case 0:  v = rnd(); break;
        case 1:  v = floor + 1u + rnd() % TFW_BOOT_FLOOR_MAX_STEP; break;
        case 2:  v = floor + TFW_BOOT_FLOOR_MAX_STEP + 1u + rnd() % 4u; break;
        case 3:  v = floor - rnd() % 4u; break;
        case 4:  v = (rnd() % 2u) ? 0xFFFFFFFFuL : 0u; break;
        default: {
            uint8_t            raw[TFW_BOOT_TRAILER_LEN];
            tfw_boot_trailer_t tr;
            memcpy(raw, good, sizeof raw);
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION], floor + rnd() % 40u);
            raw[TFW_BOOT_TR_IMAGE_VERSION + rnd() % 4u] ^= (uint8_t)(1u << (rnd() % 8u));
            v = (tfw_boot_trailer_parse(raw, &tr) == TFW_OK) ? tr.image_version : floor;
            break;
        }
        }

        TEST_ASSERT_EQUAL(v >= floor ? TFW_OK : TFW_ERR_PROTECTED_REGION,
                          tfw_boot_rollback_permit(v, floor));
        st = tfw_boot_floor_advance(v, floor, &out);
        if (st == TFW_OK) {
            TEST_ASSERT_TRUE(out >= floor);
            TEST_ASSERT_TRUE(out == floor || out == v);
            TEST_ASSERT_TRUE(out - floor <= TFW_BOOT_FLOOR_MAX_STEP);
            if (out > floor) {
                raised++;
            } else {
                held++;
            }
            floor = out;
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_RANGE, st);
            TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5uL, out);
            range++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, raised);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, held);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, range);
}

/* The handoff record sealed for the last image that passed, then every refused
 * flip and random trailer through the gate: the record is byte for byte as it
 * was, its CRC verifies, and no field carries the refused image's version or
 * measurement. Fields checked: magic, layout version, tier, CDI, measurement,
 * image version, reset cause, CRC and device seed -- the whole block. */
static void test_a_refused_image_leaves_the_handoff_record(void)
{
    uint8_t  block[TFW_BOOT_HANDOFF_LEN], cdi[TFW_BOOT_MEASURE_LEN];
    uint8_t  seed[TFW_BOOT_SEED_LEN], m_good[TFW_BOOT_MEASURE_LEN];
    uint8_t  tier, m[TFW_BOOT_MEASURE_LEN], c[TFW_BOOT_MEASURE_LEN];
    uint8_t  s[TFW_BOOT_SEED_LEN], cause;
    uint32_t ver;
    unsigned long refused = 0u;

    for (uint32_t i = 0u; i < sizeof cdi; i++) {
        cdi[i] = (uint8_t)rnd();
    }
    for (uint32_t i = 0u; i < sizeof seed; i++) {
        seed[i] = (uint8_t)rnd();
    }
    memcpy(m_good, &good[TFW_BOOT_TR_MEASUREMENT], sizeof m_good);
    tfw_boot_handoff_build(block, 2u, cdi, m_good, VERSION, TFW_RESET_WATCHDOG);
    tfw_boot_handoff_set_seed(block, seed);
    tfw_boot_mock_set_handoff(block, sizeof block);

    for (uint32_t it = 0u; it < 8u * TFW_BOOT_TRAILER_LEN + RANDOM_ITER; it++) {
        uint8_t            raw[TFW_BOOT_TRAILER_LEN];
        tfw_boot_trailer_t tr;
        tfw_status_t       st;

        memcpy(raw, good, sizeof raw);
        if (it < 8u * TFW_BOOT_TRAILER_LEN) {
            raw[it / 8u] ^= (uint8_t)(1u << (it % 8u));
        } else {
            for (uint32_t k = 1u + rnd() % 8u; k > 0u; k--) {
                raw[TFW_BOOT_TR_IMAGE_VERSION + rnd() % (TFW_BOOT_TR_SIGNED_LEN
                    - TFW_BOOT_TR_IMAGE_VERSION)] = (uint8_t)rnd();
            }
        }
        if (gate(raw, VERSION, &tr, &st) == AT_SIGNATURE) {
            continue;
        }
        refused++;

        TEST_ASSERT_EQUAL_MEMORY(block, tfw_boot_mock_handoff(), TFW_BOOT_HANDOFF_LEN);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_handoff_parse(tfw_boot_mock_handoff(),
                                                         &tier, c, m, &ver, &cause, s));
        TEST_ASSERT_EQUAL_UINT8(2u, tier);
        TEST_ASSERT_EQUAL_MEMORY(cdi, c, sizeof c);
        TEST_ASSERT_EQUAL_MEMORY(m_good, m, sizeof m);
        TEST_ASSERT_EQUAL_UINT32(VERSION, ver);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_RESET_WATCHDOG, cause);
        TEST_ASSERT_EQUAL_MEMORY(seed, s, sizeof s);
        /* Where the refused trailer named its own version or measurement, the
         * record does not carry it. */
        {
            uint32_t rv = (uint32_t)raw[TFW_BOOT_TR_IMAGE_VERSION]
                          | ((uint32_t)raw[TFW_BOOT_TR_IMAGE_VERSION + 1u] << 8)
                          | ((uint32_t)raw[TFW_BOOT_TR_IMAGE_VERSION + 2u] << 16)
                          | ((uint32_t)raw[TFW_BOOT_TR_IMAGE_VERSION + 3u] << 24);
            if (rv != VERSION) {
                TEST_ASSERT_NOT_EQUAL_UINT32(rv, ver);
            }
            if (memcmp(&raw[TFW_BOOT_TR_MEASUREMENT], m_good, sizeof m_good) != 0) {
                TEST_ASSERT_TRUE(memcmp(&raw[TFW_BOOT_TR_MEASUREMENT], m, sizeof m) != 0);
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_valid_trailer_reaches_the_signature);
    RUN_TEST(test_every_single_bit_flip_is_refused_or_changes_the_signed_tuple);
    RUN_TEST(test_random_trailers_and_slot_edges_stay_in_the_slot);
    RUN_TEST(test_the_floor_never_descends);
    RUN_TEST(test_a_refused_image_leaves_the_handoff_record);
    return UNITY_END();
}
