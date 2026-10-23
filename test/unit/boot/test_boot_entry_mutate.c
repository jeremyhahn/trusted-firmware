/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/boot_entry.c under mutation (#163): tfw_boot_request_take reads the
 * request out of the handoff region, judges it against the reset cause, and
 * clears it whatever the verdict. Each test below fails against a copy with
 * one of those steps skipped, reordered, shortened or lengthened, or with the
 * cause not passed through. test_boot_request covers the codec it calls.
 */

#include <string.h>
#include "unity.h"
#include "boot_request.h"
#include "boot_hal_mock.h"

void setUp(void) { tfw_boot_mock_reset(); }
void tearDown(void) {}

static void put_handoff(const uint8_t *req)
{
    uint8_t region[TFW_BOOT_HANDOFF_LEN];

    memset(region, 0x5A, sizeof region);
    memcpy(region, req, TFW_BOOT_REQUEST_LEN);
    tfw_boot_mock_set_handoff(region, sizeof region);
}

static void put_request(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];

    tfw_boot_request_encode(req);
    put_handoff(req);
}

static void assert_request_cleared_and_rest_kept(void)
{
    const uint8_t *h = tfw_boot_mock_handoff();
    size_t         i;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, h[i]);
    }
    for (; i < TFW_BOOT_HANDOFF_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x5Au, h[i]);
    }
}

/* Leaves the request's own valid bytes on the stack below the caller, where
 * tfw_boot_request_take's buffer will sit. A take that read fewer bytes than
 * the request would find the missing byte here, valid, and honour a request
 * the handoff region does not hold. */
static void __attribute__((noinline)) stain_stack(void)
{
    volatile uint8_t pad[512];
    uint8_t          req[TFW_BOOT_REQUEST_LEN];
    size_t           i;

    tfw_boot_request_encode(req);
    for (i = 0u; i < sizeof pad; i++) {
        pad[i] = req[i % TFW_BOOT_REQUEST_LEN];
    }
}

/* Kills: a dropped or reordered read (the clear first leaves zeros, which are
 * not a request), a dropped clear, a clear one byte short or one byte long,
 * and an inverted verdict. */
static void test_a_request_after_a_watchdog_reset_is_taken_once(void)
{
    put_request();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_take(TFW_RESET_WATCHDOG));
    assert_request_cleared_and_rest_kept();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_take(TFW_RESET_WATCHDOG));
    assert_request_cleared_and_rest_kept();
}

/* Kills: a cause that is not passed through (a constant WATCHDOG in its
 * place), and a clear only on the honoured path. */
static void test_every_other_cause_refuses_and_still_clears(void)
{
    static const tfw_reset_cause_t others[] = {
        TFW_RESET_UNKNOWN, TFW_RESET_POWER_ON, TFW_RESET_EXTERNAL,
        TFW_RESET_BROWNOUT, TFW_RESET_SOFTWARE, TFW_RESET_PANIC,
        (tfw_reset_cause_t)200,
    };
    size_t i;

    for (i = 0u; i < sizeof others / sizeof others[0]; i++) {
        put_request();
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_take(others[i]));
        assert_request_cleared_and_rest_kept();
    }
}

/* Kills: a read one byte short. The region's last request byte is wrong, and
 * the stack holds the right one. */
static void test_the_last_byte_is_read_from_the_region(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];

    tfw_boot_request_encode(req);
    req[TFW_BOOT_REQUEST_LEN - 1u] = 0x00u;
    put_handoff(req);
    stain_stack();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_take(TFW_RESET_WATCHDOG));
    assert_request_cleared_and_rest_kept();
}

/* The first byte the same way: the read starts at the region's start. */
static void test_the_first_byte_is_read_from_the_region(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];

    tfw_boot_request_encode(req);
    req[0] = 0x00u;
    put_handoff(req);
    stain_stack();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_take(TFW_RESET_WATCHDOG));
    assert_request_cleared_and_rest_kept();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_request_after_a_watchdog_reset_is_taken_once);
    RUN_TEST(test_every_other_cause_refuses_and_still_clears);
    RUN_TEST(test_the_last_byte_is_read_from_the_region);
    RUN_TEST(test_the_first_byte_is_read_from_the_region);
    return UNITY_END();
}
