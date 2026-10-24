/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A seeded mutation test for the trailer parser and the three questions asked
 * of a parsed trailer. The trailer arrives with an OTA image, over TCP or
 * serial, from whoever can reach the device: every byte of it is the sender's.
 *
 * The inputs are the literal trailer vectors, mutated -- bit flips, a span cut
 * short and zero-filled, image_len and image_version set to 0, the maximum,
 * one past it and all ones, a random magic or trailer version -- plus pure
 * random trailers. Each is parsed from a heap buffer of exactly
 * TFW_BOOT_TRAILER_LEN bytes, so under `make test-asan` a read past the
 * trailer is a fault, not a pass.
 *
 * The seed and the counts are literals: a failure reproduces bit for bit.
 */
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boot_image.h"
#include "vectors.h"

#define ITERATIONS 4000u

static uint32_t rng;

void setUp(void) { rng = 0x7F0B5EEDu; }
void tearDown(void) {}

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

static const char *const spans[] = {
    TFWV_TRAILER_ATMEGA328P_SPAN,
    TFWV_TRAILER_ATMEGA2560_SPAN,
    TFWV_TRAILER_ATMEGA32U4_SPAN,
    TFWV_TRAILER_ATTINY85_SPAN,
    TFWV_TRAILER_ESP32P4POE_SPAN,
    TFWV_TRAILER_ESP32QEMU_SPAN,
    TFWV_TRAILER_MISMATCH_SPAN,
    TFWV_TRAILER_BOARDZERO_SPAN,
    TFWV_TRAILER_RESERVEDFLAG_SPAN,
    TFWV_TRAILER_DEVIMAGE_SPAN,
};
#define SPAN_COUNT ((uint32_t)(sizeof spans / sizeof spans[0]))

static const uint32_t edges[] = {
    0u, 1u, TFW_BOOT_MAX_IMAGE_LEN, TFW_BOOT_MAX_IMAGE_LEN + 1u, 0xFFFFFFFFu,
};
#define EDGE_COUNT ((uint32_t)(sizeof edges / sizeof edges[0]))

static void make_input(uint8_t raw[TFW_BOOT_TRAILER_LEN])
{
    uint32_t ops = 1u + rnd() % 3u;

    if (rnd() % 8u == 0u) {
        for (uint32_t i = 0; i < TFW_BOOT_TRAILER_LEN; i++) {
            raw[i] = (uint8_t)rnd();
        }
        return;
    }
    memset(raw, 0, TFW_BOOT_TRAILER_LEN);
    memcpy(raw, spans[rnd() % SPAN_COUNT], TFWV_TRAILER_SPAN_LEN);
    for (uint32_t i = TFW_BOOT_TR_SIGNATURE; i < TFW_BOOT_TR_RESERVED; i++) {
        raw[i] = (uint8_t)rnd();
    }
    while (ops-- > 0u) {
        switch (rnd() % 7u) {
        case 0:
            for (uint32_t k = 1u + rnd() % 8u; k > 0u; k--) {
                raw[rnd() % TFW_BOOT_TRAILER_LEN] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            break;
        case 1:
            {
                uint32_t cut = rnd() % TFW_BOOT_TRAILER_LEN;
                memset(&raw[cut], 0, TFW_BOOT_TRAILER_LEN - cut);
            }
            break;
        case 2:
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_LEN], edges[rnd() % EDGE_COUNT]);
            break;
        case 3:
            wr_u32(&raw[TFW_BOOT_TR_IMAGE_VERSION], edges[rnd() % EDGE_COUNT]);
            break;
        case 4:
            raw[TFW_BOOT_TR_BOARD_ID]    = (uint8_t)rnd();
            raw[TFW_BOOT_TR_IMAGE_FLAGS] = (uint8_t)rnd();
            break;
        case 5:
            raw[TFW_BOOT_TR_VERSION + (rnd() % 2u)] = (uint8_t)rnd();
            break;
        default:
            raw[TFW_BOOT_TR_MAGIC + (rnd() % 4u)] = (uint8_t)rnd();
            break;
        }
    }
}

static void test_mutated_trailers_parse_inside_their_contract(void)
{
    static uint8_t scratch[TFW_BOOT_TRAILER_LEN];
    unsigned long  ok = 0u, refused = 0u, untouched = 0u;

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        uint8_t           *raw = malloc(TFW_BOOT_TRAILER_LEN);
        tfw_boot_trailer_t tr, sentinel;
        tfw_status_t       st;

        TEST_ASSERT_NOT_NULL(raw);
        make_input(scratch);
        memcpy(raw, scratch, TFW_BOOT_TRAILER_LEN);
        memset(&tr, 0xA5, sizeof tr);
        memcpy(&sentinel, &tr, sizeof sentinel);
        st = tfw_boot_trailer_parse(raw, &tr);
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_UNSUPPORTED ||
                         st == TFW_ERR_RANGE || st == TFW_ERR_CRYPTO);
        /* A refusal of any class leaves the caller's trailer as it was. */
        if (st != TFW_OK) {
            TEST_ASSERT_EQUAL_MEMORY(&sentinel, &tr, sizeof tr);
            untouched++;
        }
        if (st != TFW_OK) {
            refused++;
            free(raw);
            continue;
        }
        ok++;
        /* A trailer that parses names an image inside the region before it. */
        TEST_ASSERT_NOT_EQUAL_UINT32(0u, tr.image_version);
        TEST_ASSERT_TRUE(tr.image_len > 0u && tr.image_len <= TFW_BOOT_MAX_IMAGE_LEN);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[TFW_BOOT_TR_MEASUREMENT], tr.measurement,
                                     TFW_BOOT_MEASURE_LEN);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&raw[TFW_BOOT_TR_SIGNATURE], tr.signature,
                                     TFW_BOOT_SIG_LEN);

        st = tfw_boot_binding_permit(&tr, raw[TFW_BOOT_TR_BOARD_ID]);
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_UNSUPPORTED);
        st = tfw_boot_rollback_permit(tr.image_version, rnd());
        TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_PROTECTED_REGION);
        {
            uint32_t floor = rnd(), out = floor;
            st = tfw_boot_floor_advance(tr.image_version, floor, &out);
            TEST_ASSERT_TRUE(st == TFW_OK || st == TFW_ERR_RANGE);
            if (st == TFW_OK) {
                TEST_ASSERT_TRUE(out >= floor);
            } else {
                TEST_ASSERT_EQUAL_UINT32(floor, out);
            }
        }

        /* What parsed serialises back to bytes that parse to the same trailer. */
        {
            uint8_t            again[TFW_BOOT_TRAILER_LEN];
            tfw_boot_trailer_t tr2;

            tfw_boot_trailer_serialise(&tr, again);
            TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trailer_parse(again, &tr2));
            TEST_ASSERT_EQUAL_UINT32(tr.image_version, tr2.image_version);
            TEST_ASSERT_EQUAL_UINT32(tr.image_len, tr2.image_len);
            TEST_ASSERT_EQUAL_UINT8(tr.board_id, tr2.board_id);
            TEST_ASSERT_EQUAL_UINT8(tr.image_flags, tr2.image_flags);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(tr.measurement, tr2.measurement,
                                         TFW_BOOT_MEASURE_LEN);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(tr.signature, tr2.signature,
                                         TFW_BOOT_SIG_LEN);
        }
        free(raw);
    }

    /* Proof the loop ran and reached both sides of the parser. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, ok);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, untouched);
}

/* A trailer that passes magic and version and is then refused writes nothing:
 * the image_version and image_len checks run before any field reaches *out. */
static void refused_leaves_trailer(const uint8_t head[16], tfw_status_t want)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr, sentinel;

    memset(raw, 0, sizeof raw);
    memcpy(raw, head, 16);
    memset(&tr, 0xA5, sizeof tr);
    memcpy(&sentinel, &tr, sizeof sentinel);
    TEST_ASSERT_EQUAL(want, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &tr, sizeof tr);
}

static void test_version_zero_refused_leaves_trailer_untouched(void)
{
    static const uint8_t head[16] = {
        0x54, 0x46, 0x49, 0x4D, 0x01, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    };
    refused_leaves_trailer(head, TFW_ERR_CRYPTO);
}

static void test_length_zero_refused_leaves_trailer_untouched(void)
{
    static const uint8_t head[16] = {
        0x54, 0x46, 0x49, 0x4D, 0x01, 0x00, 0x02, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    refused_leaves_trailer(head, TFW_ERR_RANGE);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_trailers_parse_inside_their_contract);
    RUN_TEST(test_version_zero_refused_leaves_trailer_untouched);
    RUN_TEST(test_length_zero_refused_leaves_trailer_untouched);
    return UNITY_END();
}
