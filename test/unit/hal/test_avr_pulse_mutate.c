/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_pulse.c, register-level (#163).
 *
 * The line is a waveform over a model clock: a start level and a list of edge
 * times in microseconds. _delay_us() advances the clock (util/delay.h's
 * stand-in), and each read of the pin's PINx through _SFR_MEM8 returns the
 * line's level at that instant in the pin's bit, with every other bit of the
 * port the opposite level, so a read of the wrong bit sees the wrong line.
 * Nothing waits: a "timeout" of 70 000 us is 70 000 loop turns.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"

#define PINB_ADDR 0x23u
#define MAX_EDGES 8u

static const tfw_pinref_t pin = { PINB_ADDR, 5u };

static uint8_t  start_level;
static uint32_t edges[MAX_EDGES];
static unsigned n_edges;
static uint32_t now;
static unsigned bad_delays;
static uint8_t  port_byte;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    (void)id;
    TEST_FAIL_MESSAGE("hal_pulse.c names a fixed register");
    return 0;
}

static uint8_t level_at(uint32_t t)
{
    uint8_t  level = start_level;
    unsigned i;

    for (i = 0u; i < n_edges; i++) {
        if (edges[i] <= t) {
            level = (uint8_t)!level;
        }
    }
    return level;
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    TEST_ASSERT_EQUAL_HEX_MESSAGE(PINB_ADDR, addr, "hal_pulse.c read past the pin's PINx");
    port_byte = (uint8_t)(level_at(now) ? (1u << pin.bit) : ~(1u << pin.bit));
    return &port_byte;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_pulse.c made a word access");
    return 0;
}

void tfw_avr_delay_us(double us)
{
    if (us != 1.0) {
        bad_delays++;
    }
    now += (uint32_t)us;
}

/* The line at `level0` from time 0, flipping at each of the edges given. */
static void line(uint8_t level0, unsigned n, const uint32_t *at)
{
    start_level = level0;
    n_edges     = n;
    memcpy(edges, at, n * sizeof at[0]);
    now        = 0u;
    bad_delays = 0u;
}

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------- pulse_in */

static void test_pulse_in_measures_width(void)
{
    static const uint32_t e[] = { 10u, 30u };
    uint8_t               lv;

    for (lv = 0u; lv < 2u; lv++) {
        uint32_t out = 0xDEADu;

        line((uint8_t)!lv, 2u, e);     /* idle away from lv, a 20 us pulse at lv */
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_in(&pin, lv, 1000u, &out));
        TEST_ASSERT_EQUAL_UINT32(20u, out);
        TEST_ASSERT_EQUAL_UINT32(30u, now);
        TEST_ASSERT_EQUAL_UINT(0u, bad_delays);
    }
}

static void test_pulse_in_waits_out_a_pulse_in_progress(void)
{
    static const uint32_t e[] = { 5u, 15u, 40u };
    uint32_t              out = 0u;

    line(1u, 3u, e);                   /* high till 5, low till 15, high 25 us */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &out));
    TEST_ASSERT_EQUAL_UINT32(25u, out);
    TEST_ASSERT_EQUAL_UINT(0u, bad_delays);
}

static void test_pulse_in_edge_wait_bound(void)
{
    uint32_t e[2];
    uint32_t out;

    /* The edge after exactly `timeout` idle reads is in time; one later is not. */
    e[0] = 100u; e[1] = 110u;
    out = 0u;
    line(0u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(10u, out);

    e[0] = 101u; e[1] = 111u;
    out = 0xABCDu;
    line(0u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, out);
}

static void test_pulse_in_one_budget_for_both_waits(void)
{
    uint32_t e[3];
    uint32_t out;

    /* 40 us of a pulse in progress and 60 us of idle share one 100 us budget. */
    e[0] = 40u; e[1] = 100u; e[2] = 105u;
    out = 0u;
    line(1u, 3u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(5u, out);

    e[1] = 101u; e[2] = 106u;
    out = 0xABCDu;
    line(1u, 3u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, out);

    /* A pulse in progress alone longer than the budget: given up on after
     * exactly the budget, not a microsecond sooner. */
    e[0] = 101u; e[1] = 150u; e[2] = 160u;
    line(1u, 3u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, out);
    TEST_ASSERT_EQUAL_UINT32(100u, now);
}

static void test_pulse_in_width_bound(void)
{
    uint32_t e[2];
    uint32_t out;

    /* The width gets its own budget, not what the waits left of it. */
    e[0] = 50u; e[1] = 150u;
    out = 0u;
    line(0u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(100u, out);

    e[1] = 151u;
    out = 0xABCDu;
    line(0u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 100u, &out));
    TEST_ASSERT_EQUAL_UINT32(0xABCDu, out);
}

/* ----------------------------------------------------------- pulse_capture */

static void sentinel(uint16_t *out, uint8_t *n)
{
    unsigned i;

    for (i = 0u; i < MAX_EDGES; i++) {
        out[i] = 0xEEEEu;
    }
    *n = 0xEEu;
}

static void test_capture_records_each_level_until_quiet(void)
{
    static const uint32_t e[] = { 3u, 13u, 20u, 50u };
    uint16_t              out[MAX_EDGES];
    uint8_t               n, lv;

    for (lv = 0u; lv < 2u; lv++) {
        sentinel(out, &n);
        line((uint8_t)!lv, 4u, e);     /* away for 3, then 10, 7, 30, then quiet */
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, lv, out, 8u, 1000u, &n));
        TEST_ASSERT_EQUAL_UINT8(3u, n);
        TEST_ASSERT_EQUAL_UINT16(10u, out[0]);
        TEST_ASSERT_EQUAL_UINT16(7u, out[1]);
        TEST_ASSERT_EQUAL_UINT16(30u, out[2]);
        TEST_ASSERT_EQUAL_HEX16(0xEEEEu, out[3]);
        TEST_ASSERT_EQUAL_UINT32(50u + 1000u, now);   /* quiet for the whole budget */
        TEST_ASSERT_EQUAL_UINT(0u, bad_delays);
    }
}

static void test_capture_stops_at_max_edges(void)
{
    static const uint32_t e[] = { 3u, 13u, 20u, 50u };
    uint16_t              out[MAX_EDGES];
    uint8_t               n;

    sentinel(out, &n);
    line(0u, 4u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 2u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    TEST_ASSERT_EQUAL_UINT16(10u, out[0]);
    TEST_ASSERT_EQUAL_UINT16(7u, out[1]);
    TEST_ASSERT_EQUAL_HEX16(0xEEEEu, out[2]);
    TEST_ASSERT_EQUAL_UINT32(20u, now);

    sentinel(out, &n);
    line(0u, 4u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 0u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_HEX16(0xEEEEu, out[0]);
    TEST_ASSERT_EQUAL_UINT32(3u, now);                /* still lined up first */
}

static void test_capture_line_up_bound(void)
{
    uint32_t e[3] = { 100u, 105u, 300u };
    uint16_t out[MAX_EDGES];
    uint8_t  n;

    sentinel(out, &n);
    line(0u, 3u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 1u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(5u, out[0]);

    e[0] = 101u; e[1] = 106u;
    sentinel(out, &n);
    line(0u, 3u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, out, 1u, 100u, &n));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, n);                 /* nothing captured, nothing said */
    TEST_ASSERT_EQUAL_HEX16(0xEEEEu, out[0]);
}

static void test_capture_quiet_first_level_is_timeout(void)
{
    uint32_t e[2] = { 0u, 0u };
    uint16_t out[MAX_EDGES];
    uint8_t  n;

    /* High from the start and never released: no edge recorded. */
    sentinel(out, &n);
    line(1u, 0u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, out, 4u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_HEX16(0xEEEEu, out[0]);

    /* A level of exactly the budget is recorded; one longer ends the capture. */
    e[0] = 100u; e[1] = 300u;
    sentinel(out, &n);
    line(1u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 1u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(100u, out[0]);

    e[0] = 101u;
    sentinel(out, &n);
    line(1u, 2u, e);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 1u, out, 1u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
}

static void test_capture_width_saturates_at_16_bits(void)
{
    uint32_t e[4] = { 0u, 65535u, 131071u, 197071u };
    uint16_t out[MAX_EDGES];
    uint8_t  n;

    /* Levels of 65535, 65536 and 66000 us: the last two read as 65535. */
    sentinel(out, &n);
    line(0u, 4u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 3u, 70000u, &n));
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    TEST_ASSERT_EQUAL_UINT16(65535u, out[0]);
    TEST_ASSERT_EQUAL_UINT16(65535u, out[1]);
    TEST_ASSERT_EQUAL_UINT16(65535u, out[2]);

    e[1] = 65534u;
    sentinel(out, &n);
    line(0u, 4u, e);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pulse_capture(&pin, 1u, out, 1u, 70000u, &n));
    TEST_ASSERT_EQUAL_UINT16(65534u, out[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pulse_in_measures_width);
    RUN_TEST(test_pulse_in_waits_out_a_pulse_in_progress);
    RUN_TEST(test_pulse_in_edge_wait_bound);
    RUN_TEST(test_pulse_in_one_budget_for_both_waits);
    RUN_TEST(test_pulse_in_width_bound);
    RUN_TEST(test_capture_records_each_level_until_quiet);
    RUN_TEST(test_capture_stops_at_max_edges);
    RUN_TEST(test_capture_line_up_bound);
    RUN_TEST(test_capture_quiet_first_level_is_timeout);
    RUN_TEST(test_capture_width_saturates_at_16_bits);
    return UNITY_END();
}
