/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M pulse timer (src/hal/cortex-m/hal_pulse.c) and the cycle step
 * it shares with tfw_hal_delay_us (cm_cycles.h, hal_tick.c), against a model
 * SysTick and a model pin.
 *
 * The model SysTick counts down from the reload of mps2-an505's 20 MHz clock
 * and moves `model_step` cycles on every read of its current value, which is
 * the only thing that moves time. The model pin is a waveform: a level at time
 * zero and the times, in microseconds, at which it toggles.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cm_pulse_model.h"

#define CYC_PER_US  MODEL_CYC_PER_US
#define RELOAD_SPAN MODEL_RELOAD_SPAN

static uint8_t  wave_start;
static const uint32_t *wave_edges;      /* toggle times, microseconds, rising */
static size_t   wave_n;
static uint64_t wave_t0;                /* the model time the waveform starts */

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    uint64_t us;
    uint8_t  lv = wave_start;
    size_t   i;

    (void)p;
    model_gpio_read_advance();
    us = (model_now - wave_t0) / CYC_PER_US;
    for (i = 0; i < wave_n && wave_edges[i] <= us; i++) {
        lv ^= 1u;
    }
    *out = lv;
    return TFW_OK;
}

static void wave(uint8_t start, const uint32_t *edges, size_t n)
{
    wave_start = start;
    wave_edges = edges;
    wave_n     = n;
    wave_t0    = model_now;
}

static tfw_pinref_t pin;

void setUp(void)
{
    memset(&pin, 0, sizeof pin);
    /* Start mid-count, so the first reload comes early in every test. */
    model_now  = 12345u;
    model_step        = 7u;
    model_read_cycles = 0u;
    wave(0u, NULL, 0u);
}

void tearDown(void) {}

static uint32_t elapsed_us(uint64_t from)
{
    return (uint32_t)((model_now - from) / CYC_PER_US);
}

static void test_cycle_step_counts_across_a_reload(void)
{
    uint32_t last;

    model_now  = RELOAD_SPAN - 10u;    /* 9 cycles before the reload */
    model_step = 0u;
    last       = model_cvr_read();
    model_step = 25u;
    TEST_ASSERT_EQUAL_UINT32(25u, model_cycles_since(&last));
    TEST_ASSERT_EQUAL_UINT32(25u, model_cycles_since(&last));
}

static void test_delay_us_spans_reloads(void)
{
    uint64_t from = model_now;

    model_step = 997u;
    tfw_hal_delay_us(2500u);
    /* At least the delay, and at most the first read and one more (about
     * 50 us each) past it. */
    TEST_ASSERT_TRUE(model_now - from >= 2500ull * CYC_PER_US);
    TEST_ASSERT_TRUE(elapsed_us(from) <= 2500u + 2u * 997u / CYC_PER_US + 1u);
}

static void test_pulse_in_high_100us(void)
{
    static const uint32_t e[] = {50u, 150u};
    uint32_t us = 0u;

    wave(0u, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, us);
}

static void test_pulse_in_low_80us(void)
{
    static const uint32_t e[] = {30u, 110u};
    uint32_t us = 0u;

    wave(1u, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 0u, 1000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 80u, us);
}

static void test_pulse_in_skips_a_pulse_in_progress(void)
{
    static const uint32_t e[] = {50u, 200u, 300u};
    uint32_t us = 0u;

    wave(1u, e, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, us);
}

static void test_pulse_in_spans_several_reloads(void)
{
    static const uint32_t e[] = {100u, 3600u};
    uint32_t us = 0u;

    wave(0u, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 10000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 3500u, us);
}

static void test_pulse_in_spans_reloads_with_sparse_reads(void)
{
    /* The edge comes after the first few reads: a read that first sees the
     * line high is a pulse in progress, and is skipped. */
    static const uint32_t e[] = {1000u, 4500u};
    uint32_t us = 0u;

    model_step = 4999u;                /* a read every ~250 us */
    wave(0u, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 10000u, &us));
    TEST_ASSERT_UINT32_WITHIN(250u, 3500u, us);
}

static void test_pulse_in_timeout_before_the_edge(void)
{
    uint32_t us   = 77u;
    uint64_t from = model_now;

    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 1001u, elapsed_us(from));
    TEST_ASSERT_EQUAL_UINT32(77u, us);
}

static void test_pulse_in_timeout_during_the_pulse(void)
{
    static const uint32_t e[] = {100u};
    uint32_t us   = 77u;
    uint64_t from = model_now;

    wave(0u, e, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_in(&pin, 1u, 1000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 1001u, elapsed_us(from));
    TEST_ASSERT_EQUAL_UINT32(77u, us);
}

/* A DHT22's answer: idle high, 80 us low and 80 us high, then each bit 50 us
 * low and 26 us (0) or 70 us (1) high, then 50 us low and the line released. */
static void test_capture_dht_like_train(void)
{
    static const uint8_t bits[] = {1u, 0u, 1u, 1u, 0u};
    uint32_t e[2u + 2u * sizeof bits + 2u];
    uint32_t want[2u + 2u * sizeof bits + 1u];
    uint16_t got[32];
    uint8_t  n = 0u;
    uint32_t t = 20u;
    size_t   k = 0u, w = 0u, i;

    e[k++]    = t;  t += 80u;  want[w++] = 80u;
    e[k++]    = t;  t += 80u;  want[w++] = 80u;
    for (i = 0; i < sizeof bits; i++) {
        uint32_t hi = bits[i] ? 70u : 26u;

        e[k++] = t;  t += 50u;  want[w++] = 50u;
        e[k++] = t;  t += hi;   want[w++] = hi;
    }
    e[k++] = t;  t += 50u;  want[w++] = 50u;
    e[k++] = t;
    wave(1u, e, k);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, got, 32u, 200u, &n));
    TEST_ASSERT_EQUAL_UINT8(w, n);
    for (i = 0; i < w; i++) {
        TEST_ASSERT_UINT32_WITHIN(1u, want[i], got[i]);
    }
}

static void test_capture_stops_at_max_edges(void)
{
    static const uint32_t e[] = {10u, 60u, 160u, 190u, 240u, 300u};
    uint16_t got[4];
    uint8_t  n = 0u;

    wave(0u, e, 6u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, got, 4u, 500u, &n));
    TEST_ASSERT_EQUAL_UINT8(4u, n);
    TEST_ASSERT_UINT32_WITHIN(1u, 50u, got[1]);
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, got[2]);
    TEST_ASSERT_UINT32_WITHIN(1u, 30u, got[3]);
}

static void test_capture_timeout_before_the_first_level(void)
{
    uint16_t got[4];
    uint8_t  n = 9u;

    wave(1u, NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 0u, got, 4u, 500u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
}

static void test_capture_no_edge_after_the_first_level(void)
{
    uint16_t got[4];
    uint8_t  n = 9u;

    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, tfw_hal_pulse_capture(&pin, 0u, got, 4u, 500u, &n));
    TEST_ASSERT_EQUAL_UINT8(0u, n);
}

/* Time passes on the GPIO reads, not only on SysTick's: a read every ~100 us,
 * so each edge below is seen on the read after the one before it. */
static void reads_take_100us(void)
{
    model_step        = 1u;
    model_read_cycles = 100u * CYC_PER_US;
}

static void test_pulse_in_edges_on_consecutive_reads(void)
{
    /* Reads at ~100 (low), ~200 (high), ~300 (low) us. */
    static const uint32_t e[] = {150u, 250u};
    uint32_t us = 0u;

    reads_take_100us();
    wave(0u, e, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_in(&pin, 1u, 10000u, &us));
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, us);
}

static void test_capture_transitions_on_consecutive_reads(void)
{
    /* Reads at ~100 (high), ~200 (low: lined up), ~300 (high), ~400 (low)
     * us, then the line stays low until the timeout. */
    static const uint32_t e[] = {150u, 250u, 350u};
    uint16_t got[8];
    uint8_t  n = 0u;

    reads_take_100us();
    wave(1u, e, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pulse_capture(&pin, 0u, got, 8u, 200u, &n));
    TEST_ASSERT_EQUAL_UINT8(2u, n);
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, got[0]);
    TEST_ASSERT_UINT32_WITHIN(1u, 100u, got[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cycle_step_counts_across_a_reload);
    RUN_TEST(test_delay_us_spans_reloads);
    RUN_TEST(test_pulse_in_high_100us);
    RUN_TEST(test_pulse_in_low_80us);
    RUN_TEST(test_pulse_in_skips_a_pulse_in_progress);
    RUN_TEST(test_pulse_in_spans_several_reloads);
    RUN_TEST(test_pulse_in_spans_reloads_with_sparse_reads);
    RUN_TEST(test_pulse_in_timeout_before_the_edge);
    RUN_TEST(test_pulse_in_timeout_during_the_pulse);
    RUN_TEST(test_capture_dht_like_train);
    RUN_TEST(test_capture_stops_at_max_edges);
    RUN_TEST(test_capture_timeout_before_the_first_level);
    RUN_TEST(test_capture_no_edge_after_the_first_level);
    RUN_TEST(test_pulse_in_edges_on_consecutive_reads);
    RUN_TEST(test_capture_transitions_on_consecutive_reads);
    return UNITY_END();
}
