/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The update-session request, bootloader side.
 *
 * What matters is what is NOT honoured. A request that opened a session after a
 * power cycle, or on garbage, would put a healthy device in the bootloader for
 * no reason anyone asked, so most of these are the ways a request is refused.
 * The encoding is pinned byte for byte and the check is held against every
 * reset cause and every single-bit damage to the eight bytes.
 *
 * Mutants this suite kills (#163; each tried against the unit and seen to
 * fail it):
 *   R1  valid: the reset-cause check dropped
 *   R2  valid: `!= WATCHDOG` -> `< WATCHDOG` (a later cause honoured)
 *   R3  valid: the magic compare inverted
 *   R4  valid: the magic check dropped
 *   R5  valid: the inverse check dropped (any inverse honoured)
 *   R6  valid: the inverse read at the magic's offset
 *   R7  encode: the magic and the inverse written at each other's offset
 *   R8  encode: the inverse not written
 *   R9  encode: the magic written in both places
 */

#include <string.h>
#include "unity.h"
#include "boot_request.h"
#include "boot_hal_mock.h"

/* The request as it is on the wire, little-endian: 'U','P','D','!' and its
 * complement. A literal, so a moved encoding fails here. */
static const uint8_t WIRE[TFW_BOOT_REQUEST_LEN] = {
    0x55u, 0x50u, 0x44u, 0x21u, 0xAAu, 0xAFu, 0xBBu, 0xDEu
};

void setUp(void) { tfw_boot_mock_reset(); }
void tearDown(void) {}

static int region_is_zero(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (p[i] != 0u) { return 0; }
    }
    return 1;
}

static void put_request(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];
    tfw_boot_request_encode(req);
    tfw_boot_mock_set_handoff(req, sizeof req);
}

/* ------------------------------------------------------------- the codec */

/* Pinned as literals: the application and the bootloader are built apart, and
 * a layout that moved in only one of them would be a request nobody honours. */
static void test_the_encoding_is_the_magic_then_its_complement(void)
{
    static const uint8_t want[TFW_BOOT_REQUEST_LEN] = {
        0x55, 0x50, 0x44, 0x21,   /* 'U' 'P' 'D' '!' */
        0xAA, 0xAF, 0xBB, 0xDE
    };
    uint8_t got[TFW_BOOT_REQUEST_LEN];

    tfw_boot_request_encode(got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, TFW_BOOT_REQUEST_LEN);
}

/* R7-R9 */
static void test_encode_writes_the_literal_and_nothing_else(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN + 8u];
    uint8_t want[sizeof region];

    memset(region, 0x5Au, sizeof region);
    memset(want, 0x5Au, sizeof want);
    memcpy(&want[4], WIRE, sizeof WIRE);
    tfw_boot_request_encode(&region[4]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, region, sizeof region);
}

/* What encode writes, valid honours: the two agree. */
static void test_encode_round_trips_through_valid(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    memset(region, 0, sizeof region);
    tfw_boot_request_encode(region);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_SOFTWARE));
}

static void test_the_power_on_fill_patterns_are_not_a_request(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    memset(region, 0x00, sizeof region);
    TEST_ASSERT_FALSE(tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    memset(region, 0xFF, sizeof region);
    TEST_ASSERT_FALSE(tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    memset(region, 0xAA, sizeof region);
    TEST_ASSERT_FALSE(tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
}

/* R3-R6: the complement is the check: every single-bit damage to either word
 * is refused. */
static void test_every_single_bit_damage_is_refused(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    for (unsigned bit = 0u; bit < 8u * TFW_BOOT_REQUEST_LEN; bit++) {
        memcpy(region, WIRE, sizeof region);
        region[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    }
}

/* R4-R6: the magic alone, the inverse alone, the two swapped, and the magic
 * in both places are each refused. */
static void test_half_a_request_is_refused(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    memset(region, 0, sizeof region);
    memcpy(region, WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memset(region, 0, sizeof region);
    memcpy(&region[4], &WIRE[4], 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memcpy(region, &WIRE[4], 4u);
    memcpy(&region[4], WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memcpy(region, WIRE, 4u);
    memcpy(&region[4], WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memset(region, 0xFFu, sizeof region);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    memset(region, 0x00u, sizeof region);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
}

/* R1, R2: the literal is honoured on a watchdog reset and on no other cause,
 * including the values past the named ones. */
static void test_valid_only_on_a_watchdog_reset(void)
{
    for (unsigned c = 0u; c < 16u; c++) {
        TEST_ASSERT_EQUAL_UINT8(c == (unsigned)TFW_RESET_WATCHDOG ? 1u : 0u,
                                tfw_boot_request_valid(WIRE, (tfw_reset_cause_t)c));
    }
}

/* Only the application's RESET causes a watchdog reset with nothing else set.
 * A power cycle that happened to leave the bytes intact, a brown-out, or a
 * press of the reset button must not open a session. */
static void test_only_a_pure_watchdog_reset_honours_a_request(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];
    tfw_boot_request_encode(req);

    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_UNKNOWN));
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_POWER_ON));
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_EXTERNAL));
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_BROWNOUT));
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_SOFTWARE));
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, TFW_RESET_PANIC));
    /* A cause this build has never heard of -- a future HAL's -- is not a
     * watchdog reset either. */
    TEST_ASSERT_FALSE(tfw_boot_request_valid(req, (tfw_reset_cause_t)200));
    /* "Watchdog and something else" cannot be expressed here: the HAL reports a
     * mixed reset as the other cause (see test_reset_cause.c), so the gate's
     * "nothing else set" holds by construction. */
}

/* ------------------------------------------------------------------ take */

static void test_take_honours_a_request_and_clears_it(void)
{
    put_request();
    TEST_ASSERT_TRUE(tfw_boot_request_take(TFW_RESET_WATCHDOG));
    TEST_ASSERT_TRUE(region_is_zero(tfw_boot_mock_handoff(), TFW_BOOT_REQUEST_LEN));
}

/* One shot: the second reset boots normally even though nothing else cleared it. */
static void test_a_request_is_honoured_once(void)
{
    put_request();
    TEST_ASSERT_TRUE(tfw_boot_request_take(TFW_RESET_WATCHDOG));
    TEST_ASSERT_FALSE(tfw_boot_request_take(TFW_RESET_WATCHDOG));
}

/* Cleared even when refused, or a request left by an earlier reset would wait
 * for the next watchdog reset -- which might be the application's real fault. */
static void test_take_clears_a_request_it_refuses(void)
{
    put_request();
    TEST_ASSERT_FALSE(tfw_boot_request_take(TFW_RESET_POWER_ON));
    TEST_ASSERT_TRUE(region_is_zero(tfw_boot_mock_handoff(), TFW_BOOT_REQUEST_LEN));
    TEST_ASSERT_FALSE(tfw_boot_request_take(TFW_RESET_WATCHDOG));
}

static void test_take_ignores_garbage(void)
{
    uint8_t junk[TFW_BOOT_REQUEST_LEN] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    tfw_boot_mock_set_handoff(junk, sizeof junk);
    TEST_ASSERT_FALSE(tfw_boot_request_take(TFW_RESET_WATCHDOG));
    TEST_ASSERT_TRUE(region_is_zero(tfw_boot_mock_handoff(), TFW_BOOT_REQUEST_LEN));
}

/* The request lives in the handoff region, and the bytes after it are not its
 * to touch -- tfw-boot writes the CDI handoff over the whole region later, but
 * a take must not scribble past what it owns. */
static void test_take_leaves_the_rest_of_the_region_alone(void)
{
    uint8_t before[TFW_BOOT_HANDOFF_LEN];

    memset(before, 0x5A, sizeof before);
    tfw_boot_mock_set_handoff(before, sizeof before);
    put_request();
    (void)tfw_boot_request_take(TFW_RESET_WATCHDOG);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(&before[TFW_BOOT_REQUEST_LEN],
                                  &tfw_boot_mock_handoff()[TFW_BOOT_REQUEST_LEN],
                                  TFW_BOOT_HANDOFF_LEN - TFW_BOOT_REQUEST_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_encoding_is_the_magic_then_its_complement);
    RUN_TEST(test_encode_writes_the_literal_and_nothing_else);
    RUN_TEST(test_encode_round_trips_through_valid);
    RUN_TEST(test_the_power_on_fill_patterns_are_not_a_request);
    RUN_TEST(test_every_single_bit_damage_is_refused);
    RUN_TEST(test_half_a_request_is_refused);
    RUN_TEST(test_valid_only_on_a_watchdog_reset);
    RUN_TEST(test_only_a_pure_watchdog_reset_honours_a_request);
    RUN_TEST(test_take_honours_a_request_and_clears_it);
    RUN_TEST(test_a_request_is_honoured_once);
    RUN_TEST(test_take_clears_a_request_it_refuses);
    RUN_TEST(test_take_ignores_garbage);
    RUN_TEST(test_take_leaves_the_rest_of_the_region_alone);
    return UNITY_END();
}
