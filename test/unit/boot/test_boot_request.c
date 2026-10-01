/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The update-session request, bootloader side.
 *
 * What matters is what is NOT honoured. A request that opened a session after a
 * power cycle, or on garbage, would put a healthy device in the bootloader for
 * no reason anyone asked, so most of these are the ways a request is refused.
 */

#include <string.h>
#include "unity.h"
#include "boot_request.h"
#include "boot_hal_mock.h"
#include "reset_cause.h"

void setUp(void) { ag_boot_mock_reset(); }
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
    uint8_t req[AG_BOOT_REQUEST_LEN];
    ag_boot_request_encode(req);
    ag_boot_mock_set_handoff(req, sizeof req);
}

/* ------------------------------------------------------------- the codec */

/* Pinned as literals: the application and the bootloader are built apart, and
 * a layout that moved in only one of them would be a request nobody honours. */
static void test_the_encoding_is_the_magic_then_its_complement(void)
{
    static const uint8_t want[AG_BOOT_REQUEST_LEN] = {
        0x55, 0x50, 0x44, 0x21,   /* 'U' 'P' 'D' '!' */
        0xAA, 0xAF, 0xBB, 0xDE
    };
    uint8_t got[AG_BOOT_REQUEST_LEN];

    ag_boot_request_encode(got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, AG_BOOT_REQUEST_LEN);
}

static void test_an_encoded_request_after_a_watchdog_reset_is_valid(void)
{
    uint8_t req[AG_BOOT_REQUEST_LEN];
    ag_boot_request_encode(req);
    TEST_ASSERT_TRUE(ag_boot_request_valid(req, AG_MCUSR_WDRF));
}

static void test_the_power_on_fill_patterns_are_not_a_request(void)
{
    uint8_t region[AG_BOOT_REQUEST_LEN];

    memset(region, 0x00, sizeof region);
    TEST_ASSERT_FALSE(ag_boot_request_valid(region, AG_MCUSR_WDRF));
    memset(region, 0xFF, sizeof region);
    TEST_ASSERT_FALSE(ag_boot_request_valid(region, AG_MCUSR_WDRF));
    memset(region, 0xAA, sizeof region);
    TEST_ASSERT_FALSE(ag_boot_request_valid(region, AG_MCUSR_WDRF));
}

/* The complement is the check: a magic alone, or a complement alone, or either
 * with a single bit out of place, is not a request. */
static void test_a_damaged_request_is_not_valid(void)
{
    uint8_t req[AG_BOOT_REQUEST_LEN];
    size_t  i;

    ag_boot_request_encode(req);
    for (i = 0u; i < AG_BOOT_REQUEST_LEN; i++) {
        req[i] ^= 0x01u;
        TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF));
        req[i] ^= 0x01u;
    }

    ag_boot_request_encode(req);
    memset(&req[AG_BOOT_REQ_OFF_INVERSE], 0, 4u);
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF));

    ag_boot_request_encode(req);
    memset(&req[AG_BOOT_REQ_OFF_MAGIC], 0, 4u);
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF));
}

/* Only the application's RESET causes a watchdog reset with nothing else set.
 * A power cycle that happened to leave the bytes intact, a brown-out, or a
 * press of the reset button must not open a session. */
static void test_only_a_pure_watchdog_reset_honours_a_request(void)
{
    uint8_t req[AG_BOOT_REQUEST_LEN];
    ag_boot_request_encode(req);

    TEST_ASSERT_FALSE(ag_boot_request_valid(req, 0u));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_PORF));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_EXTRF));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_BORF));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF | AG_MCUSR_PORF));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF | AG_MCUSR_EXTRF));
    TEST_ASSERT_FALSE(ag_boot_request_valid(req, AG_MCUSR_WDRF | AG_MCUSR_BORF));
}

/* ------------------------------------------------------------------ take */

static void test_take_honours_a_request_and_clears_it(void)
{
    put_request();
    TEST_ASSERT_TRUE(ag_boot_request_take(AG_MCUSR_WDRF));
    TEST_ASSERT_TRUE(region_is_zero(ag_boot_mock_handoff(), AG_BOOT_REQUEST_LEN));
}

/* One shot: the second reset boots normally even though nothing else cleared it. */
static void test_a_request_is_honoured_once(void)
{
    put_request();
    TEST_ASSERT_TRUE(ag_boot_request_take(AG_MCUSR_WDRF));
    TEST_ASSERT_FALSE(ag_boot_request_take(AG_MCUSR_WDRF));
}

/* Cleared even when refused, or a request left by an earlier reset would wait
 * for the next watchdog reset -- which might be the application's real fault. */
static void test_take_clears_a_request_it_refuses(void)
{
    put_request();
    TEST_ASSERT_FALSE(ag_boot_request_take(AG_MCUSR_PORF));
    TEST_ASSERT_TRUE(region_is_zero(ag_boot_mock_handoff(), AG_BOOT_REQUEST_LEN));
    TEST_ASSERT_FALSE(ag_boot_request_take(AG_MCUSR_WDRF));
}

static void test_take_ignores_garbage(void)
{
    uint8_t junk[AG_BOOT_REQUEST_LEN] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ag_boot_mock_set_handoff(junk, sizeof junk);
    TEST_ASSERT_FALSE(ag_boot_request_take(AG_MCUSR_WDRF));
    TEST_ASSERT_TRUE(region_is_zero(ag_boot_mock_handoff(), AG_BOOT_REQUEST_LEN));
}

/* The request lives in the handoff region, and the bytes after it are not its
 * to touch -- ag-boot writes the CDI handoff over the whole region later, but
 * a take must not scribble past what it owns. */
static void test_take_leaves_the_rest_of_the_region_alone(void)
{
    uint8_t before[AG_BOOT_HANDOFF_LEN];

    memset(before, 0x5A, sizeof before);
    ag_boot_mock_set_handoff(before, sizeof before);
    put_request();
    (void)ag_boot_request_take(AG_MCUSR_WDRF);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(&before[AG_BOOT_REQUEST_LEN],
                                  &ag_boot_mock_handoff()[AG_BOOT_REQUEST_LEN],
                                  AG_BOOT_HANDOFF_LEN - AG_BOOT_REQUEST_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_encoding_is_the_magic_then_its_complement);
    RUN_TEST(test_an_encoded_request_after_a_watchdog_reset_is_valid);
    RUN_TEST(test_the_power_on_fill_patterns_are_not_a_request);
    RUN_TEST(test_a_damaged_request_is_not_valid);
    RUN_TEST(test_only_a_pure_watchdog_reset_honours_a_request);
    RUN_TEST(test_take_honours_a_request_and_clears_it);
    RUN_TEST(test_a_request_is_honoured_once);
    RUN_TEST(test_take_clears_a_request_it_refuses);
    RUN_TEST(test_take_ignores_garbage);
    RUN_TEST(test_take_leaves_the_rest_of_the_region_alone);
    return UNITY_END();
}
