/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 pulse timing, src/hal/esp32/hal_pulse.c.
 *
 * The unit had no host build, and no test_pulse_mutate exists. This builds
 * it over stand-ins for ESP-IDF's gpio driver and esp_timer
 * (test/mock/esp32/pulse/) that play a scripted waveform against a virtual
 * microsecond clock: each read of the pin samples the waveform at the clock
 * and moves the clock on by one, and the clock starts far from zero so a
 * narrowed time is seen. It holds:
 *
 *   - a pulse as the time from its leading to its trailing edge, a pulse
 *     already in progress skipped, any non-zero level high;
 *   - each of the three waits bounded by one deadline, an edge on the
 *     deadline seen and one past it a timeout, the result left alone;
 *   - a capture as the interval between successive edges after the start
 *     level, ending at max_edges or when the line goes quiet, each interval
 *     clamped to 16 bits, one edge a capture, nothing captured a timeout
 *     with a count of 0;
 *   - every read on the caller's pin.
 */

#include <stdint.h>
#include "unity.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "tfw/hal.h"

#define BASE      1000000000000LL   /* 2^39 and more: no 32-bit clock holds it */
#define MAX_READS 2000000u

static int64_t        now;
static int            level0;
static const int64_t *edges;
static unsigned       nedges;
static unsigned       nreads;
static int            pin_seen;

int gpio_get_level(gpio_num_t gpio_num)
{
    int      level = level0;
    unsigned i;

    TEST_ASSERT_EQUAL_INT(pin_seen, gpio_num);
    TEST_ASSERT_LESS_THAN_UINT(MAX_READS, nreads);
    for (i = 0u; i < nedges; i++) {
        if (BASE + edges[i] <= now) {
            level = !level;
        }
    }
    nreads++;
    now++;
    return level;
}

int64_t esp_timer_get_time(void)
{
    return now;
}

static void wave(int start, const int64_t *e, unsigned n)
{
    now    = BASE;
    level0 = start;
    edges  = e;
    nedges = n;
    nreads = 0u;
}

void setUp(void)
{
    pin_seen = 21;
    wave(0, NULL, 0u);
}

void tearDown(void) {}

static const tfw_pinref_t pin = { 21u };

static void test_a_pulse_is_its_leading_to_trailing_edge(void)
{
    static const int64_t e[] = { 10, 25 };
    uint32_t out = 0u;

    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &out));
    TEST_ASSERT_EQUAL_UINT32(15u, out);

    wave(1, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 0u, 1000u, &out));
    TEST_ASSERT_EQUAL_UINT32(15u, out);

    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 2u, 1000u, &out));
    TEST_ASSERT_EQUAL_UINT32(15u, out);
}

static void test_a_pulse_in_progress_is_skipped(void)
{
    static const int64_t e[] = { 5, 10, 17 };
    uint32_t out = 0u;

    wave(1, e, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &out));
    TEST_ASSERT_EQUAL_UINT32(7u, out);
}

static void test_a_long_pulse_is_not_narrowed(void)
{
    static const int64_t e[] = { 10, 10 + 100000 };
    uint32_t out = 0u;

    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 0xFFFFFFFFu, &out));
    TEST_ASSERT_EQUAL_UINT32(100000u, out);
}

static void test_an_edge_on_the_deadline_is_seen(void)
{
    static const int64_t e[] = { 10, 20 };
    uint32_t out = 0u;

    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 20u, &out));
    TEST_ASSERT_EQUAL_UINT32(10u, out);
}

static void test_an_edge_past_the_deadline_is_a_timeout(void)
{
    static const int64_t e[] = { 10, 20 };
    uint32_t out = 0xDEADBEEFu;

    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 19u, &out));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, out);
    TEST_ASSERT_EQUAL_UINT(20u, nreads);
}

static void test_each_wait_is_bounded(void)
{
    static const int64_t rise[] = { 10 };
    uint32_t out = 0xDEADBEEFu;

    /* never leaves the level: the wait for the idle level */
    wave(1, NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 50u, &out));
    TEST_ASSERT_EQUAL_UINT(51u, nreads);
    /* never rises: the wait for the leading edge */
    wave(0, NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 50u, &out));
    TEST_ASSERT_EQUAL_UINT(51u, nreads);
    /* never falls: the wait for the trailing edge */
    wave(0, rise, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 50u, &out));
    TEST_ASSERT_EQUAL_UINT(51u, nreads);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, out);
}

static void test_every_read_is_on_the_callers_pin(void)
{
    static const int64_t e[] = { 3, 6, 9 };
    const tfw_pinref_t   p = { 47u };
    uint32_t out = 0u;
    uint16_t us[4];
    uint8_t  n = 0u;

    pin_seen = 47;
    wave(0, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&p, 1u, 100u, &out));
    wave(0, e, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&p, 0u, us, 4u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(3u, n);
}

static void test_a_capture_is_the_intervals_between_edges(void)
{
    static const int64_t e[] = { 10, 30, 33, 100 };
    uint16_t us[6] = { 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu };
    uint8_t  n = 0xAAu;

    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 5u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(4u, n);
    TEST_ASSERT_EQUAL_UINT16(10u, us[0]);
    TEST_ASSERT_EQUAL_UINT16(20u, us[1]);
    TEST_ASSERT_EQUAL_UINT16(3u, us[2]);
    TEST_ASSERT_EQUAL_UINT16(67u, us[3]);
    TEST_ASSERT_EQUAL_HEX16(0xAAAAu, us[4]);
}

static void test_a_capture_of_one_edge_is_a_capture(void)
{
    static const int64_t e[] = { 12 };
    uint16_t us[2] = { 0xAAAAu, 0xAAAAu };
    uint8_t  n = 0xAAu;

    wave(0, e, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 2u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(1u, n);
    TEST_ASSERT_EQUAL_UINT16(12u, us[0]);
    TEST_ASSERT_EQUAL_HEX16(0xAAAAu, us[1]);
}

static void test_a_capture_waits_for_its_start_level(void)
{
    static const int64_t e[] = { 5, 15, 40 };
    uint8_t  levels[] = { 1u, 2u };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        uint16_t us[4] = { 0u, 0u, 0u, 0u };
        uint8_t  n = 0u;

        wave(0, e, 3u);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, levels[i], us, 4u, 1000u, &n));
        TEST_ASSERT_EQUAL_UINT8(2u, n);
        TEST_ASSERT_EQUAL_UINT16(10u, us[0]);
        TEST_ASSERT_EQUAL_UINT16(25u, us[1]);
    }
}

static void test_a_capture_stops_at_max_edges(void)
{
    static const int64_t e[] = { 10, 20, 30, 40 };
    uint16_t us[4] = { 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu };
    uint8_t  n = 0u;

    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 2u, 1000u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    TEST_ASSERT_EQUAL_UINT16(10u, us[0]);
    TEST_ASSERT_EQUAL_UINT16(10u, us[1]);
    TEST_ASSERT_EQUAL_HEX16(0xAAAAu, us[2]);
    TEST_ASSERT_EQUAL_UINT(21u, nreads);
}

static void test_a_capture_of_no_edges_is_a_timeout_of_0(void)
{
    static const int64_t e[] = { 10 };
    uint16_t us[2] = { 0xAAAAu, 0xAAAAu };
    uint8_t  n = 0xAAu;

    /* max_edges 0 */
    wave(0, e, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 0u, us, 0u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    /* the line never moves after the start level */
    n = 0xAAu;
    wave(0, NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 0u, us, 2u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT(101u, nreads);
    /* the start level never comes */
    n = 0xAAu;
    wave(1, NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 0u, us, 2u, 100u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
    TEST_ASSERT_EQUAL_UINT(101u, nreads);
    TEST_ASSERT_EQUAL_HEX16(0xAAAAu, us[0]);
}

static void test_a_capture_ends_at_the_deadline(void)
{
    static const int64_t e[] = { 10, 20, 30, 40 };
    uint16_t us[4];
    uint8_t  n = 0u;

    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 4u, 30u, &n));
    TEST_ASSERT_EQUAL_UINT8(3u, n);
    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 4u, 29u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 4u, 0xFFFFFFFFu, &n));
    TEST_ASSERT_EQUAL_UINT8(4u, n);
}

static void test_an_interval_is_clamped_to_16_bits(void)
{
    static const int64_t e[] = { 0xFFFE, 0xFFFE + 0xFFFF, 0xFFFE + 0xFFFF + 0x10000,
                                 0xFFFE + 0xFFFF + 0x10000 + 0x20000 };
    uint16_t us[4] = { 0u, 0u, 0u, 0u };
    uint8_t  n = 0u;

    wave(0, e, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, us, 4u, 0xFFFFFFFFu, &n));
    TEST_ASSERT_EQUAL_UINT8(4u, n);
    TEST_ASSERT_EQUAL_HEX16(0xFFFEu, us[0]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, us[1]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, us[2]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, us[3]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pulse_is_its_leading_to_trailing_edge);
    RUN_TEST(test_a_pulse_in_progress_is_skipped);
    RUN_TEST(test_a_long_pulse_is_not_narrowed);
    RUN_TEST(test_an_edge_on_the_deadline_is_seen);
    RUN_TEST(test_an_edge_past_the_deadline_is_a_timeout);
    RUN_TEST(test_each_wait_is_bounded);
    RUN_TEST(test_every_read_is_on_the_callers_pin);
    RUN_TEST(test_a_capture_is_the_intervals_between_edges);
    RUN_TEST(test_a_capture_of_one_edge_is_a_capture);
    RUN_TEST(test_a_capture_waits_for_its_start_level);
    RUN_TEST(test_a_capture_stops_at_max_edges);
    RUN_TEST(test_a_capture_of_no_edges_is_a_timeout_of_0);
    RUN_TEST(test_a_capture_ends_at_the_deadline);
    RUN_TEST(test_an_interval_is_clamped_to_16_bits);
    return UNITY_END();
}
