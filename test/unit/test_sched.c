/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The pin scheduler, including the millisecond wrap.
 *
 * Time is a variable the test sets, so the 49.7-day rollover -- which on real
 * hardware takes 49.7 days to reach and then misbehaves for a few milliseconds
 * twice a year -- is one assignment and a deterministic assertion.
 */

#include <string.h>
#include "unity.h"
#include "sched.h"
#include "gpio.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

/* A pin with no other job: D2, PORTD bit 2. */
#define PIN     2u
#define PIN_PORT TFW_ADDR_PIND
#define PIN_BIT  2u

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sched_init();
}
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t timer_set(tfw_pin_t pin, uint8_t level, uint32_t ms)
{
    uint8_t p[6] = { pin, level,
                     (uint8_t)(ms), (uint8_t)(ms >> 8),
                     (uint8_t)(ms >> 16), (uint8_t)(ms >> 24) };
    return run(TFW_CMD_TIMER_SET, p, 6u);
}

static uint8_t level_now(void) { return tfw_mock_pin_level(PIN_PORT, PIN_BIT); }

/* The headline behaviour: drive high, and be driven low again by the device
 * itself -- no host involvement, which is the point. */
static void test_a_timed_pin_reverts_without_the_host(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());
    TEST_ASSERT_TRUE(tfw_mock_pin_is_output(PIN_PORT, PIN_BIT));

    tfw_mock_advance(4999u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());   /* one millisecond early */

    tfw_mock_advance(1u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());   /* and exactly on time */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
}

/* The complement case: hold low, revert high. A scheduler that only knew how to
 * turn things off would be a one-way switch, not a primitive. */
static void test_a_timed_low_reverts_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 0u, 100u));
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
    tfw_mock_advance(100u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());
}

/*
 * THE ROLLOVER.
 *
 * Time is set just below the wrap so the deadline lands on the other side of
 * it. With `now >= deadline` this fires immediately -- seven weeks after a
 * device is deployed, every armed timer would expire at once. The
 * signed-difference form is correct across the wrap, and this is the test that
 * says so.
 */
static void test_a_deadline_across_the_millisecond_wrap_is_honoured(void)
{
    tfw_mock_set_millis(0xFFFFFF00u);            /* 256 ms before the wrap */
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 1000u));  /* deadline wraps to 0x2E8 */

    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());   /* must NOT have fired */

    tfw_mock_advance(999u);                      /* now 0x2E7: still one short */
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());

    tfw_mock_advance(1u);                        /* now 0x2E8 */
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
}

/* Re-arming extends rather than consuming a second slot. A host that re-issues
 * a command -- which a periodic controller does constantly -- would
 * otherwise exhaust the table and begin failing, having done nothing wrong. */
static void test_rearming_a_pin_extends_it_and_does_not_consume_a_slot(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_sched_active());

    tfw_mock_advance(900u);
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 1000u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_sched_active());   /* still one */

    tfw_mock_advance(900u);       /* past the FIRST deadline */
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());         /* extended, not expired */

    tfw_mock_advance(200u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
}

static void test_the_slot_table_fills_and_then_refuses(void)
{
    for (uint8_t i = 0u; i < TFW_SCHED_SLOTS; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, timer_set((tfw_pin_t)(i + 2u), 1u, 1000u));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_SCHED_SLOTS, tfw_sched_active());
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, timer_set(12u, 1u, 1000u));
}

static void test_a_zero_duration_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, timer_set(PIN, 1u, 0u));
}

/* Beyond 2^31 ms the signed-difference comparison can no longer tell due from
 * not-yet. Refused at arm time, where it is a clean error, rather than
 * discovered at runtime as a pin that never reverts. */
static void test_a_duration_past_the_signed_bound_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, timer_set(PIN, 1u, 0x80000000u));
    TEST_ASSERT_EQUAL(TFW_OK,        timer_set(PIN, 1u, 0x7FFFFFFFu));
}

static void test_a_reserved_pin_cannot_be_scheduled(void)
{
    /* Pin 1 is the UART transmitter. Driving it would cut the line the command
     * arrived on, and the host would never learn why the device went quiet. */
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, timer_set(1u, 1u, 1000u));
}

static void test_cancel_can_leave_the_pin_or_apply_the_final_level(void)
{
    uint8_t p[2];

    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    p[0] = PIN; p[1] = 0u;                       /* leave it where it is */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_CANCEL, p, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, level_now());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());

    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    p[1] = 1u;                                   /* apply the final level now */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_CANCEL, p, 2u));
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
}

static void test_cancelling_a_pin_with_no_timer_is_refused(void)
{
    uint8_t p[2] = { PIN, 0u };
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, run(TFW_CMD_TIMER_CANCEL, p, 2u));
}

static void test_query_reports_the_remaining_time(void)
{
    uint8_t p[1] = { PIN };
    uint32_t remaining;

    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    tfw_mock_advance(1500u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_QUERY, p, 1u));

    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);      /* active */
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[1]);      /* held high */
    remaining = (uint32_t)outbuf[2] | ((uint32_t)outbuf[3] << 8)
              | ((uint32_t)outbuf[4] << 16) | ((uint32_t)outbuf[5] << 24);
    TEST_ASSERT_EQUAL_UINT32(3500u, remaining);
}

static void test_query_on_an_unscheduled_pin_reports_inactive(void)
{
    uint8_t p[1] = { PIN };
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_QUERY, p, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
}

/* A deadline that has passed but not yet been swept must report zero remaining,
 * not the four billion milliseconds an unsigned underflow would produce. */
static void test_query_clamps_an_overdue_deadline_to_zero(void)
{
    uint8_t p[1] = { PIN };
    uint32_t remaining;

    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 100u));
    tfw_mock_advance(500u);                       /* overdue, not yet swept */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TIMER_QUERY, p, 1u));
    remaining = (uint32_t)outbuf[2] | ((uint32_t)outbuf[3] << 8)
              | ((uint32_t)outbuf[4] << 16) | ((uint32_t)outbuf[5] << 24);
    TEST_ASSERT_EQUAL_UINT32(0u, remaining);
}

/* Several pins, different deadlines, one sweep. Each must fire on its own
 * schedule -- a sweep that expired the whole table together would be much
 * harder to notice than one that expired none of it. */
static void test_independent_deadlines_fire_independently(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(2u, 1u, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(3u, 1u, 300u));
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(4u, 1u, 200u));

    tfw_mock_advance(150u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(TFW_ADDR_PIND, 4u));

    tfw_mock_advance(60u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 4u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_pin_level(TFW_ADDR_PIND, 3u));

    tfw_mock_advance(100u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_timed_pin_reverts_without_the_host);
    RUN_TEST(test_a_timed_low_reverts_high);
    RUN_TEST(test_a_deadline_across_the_millisecond_wrap_is_honoured);
    RUN_TEST(test_rearming_a_pin_extends_it_and_does_not_consume_a_slot);
    RUN_TEST(test_the_slot_table_fills_and_then_refuses);
    RUN_TEST(test_a_zero_duration_is_refused);
    RUN_TEST(test_a_duration_past_the_signed_bound_is_refused);
    RUN_TEST(test_a_reserved_pin_cannot_be_scheduled);
    RUN_TEST(test_cancel_can_leave_the_pin_or_apply_the_final_level);
    RUN_TEST(test_cancelling_a_pin_with_no_timer_is_refused);
    RUN_TEST(test_query_reports_the_remaining_time);
    RUN_TEST(test_query_on_an_unscheduled_pin_reports_inactive);
    RUN_TEST(test_query_clamps_an_overdue_deadline_to_zero);
    RUN_TEST(test_independent_deadlines_fire_independently);
    return UNITY_END();
}
