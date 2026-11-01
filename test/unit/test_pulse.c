/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Pulse measurement and edge capture.
 *
 * The capture test decodes a real DHT22 waveform, because that is the whole
 * justification for the primitive: a sensor whose bits are 26 versus 70
 * microseconds cannot be read by a protocol with a round trip per bit, so
 * "the host decodes the sensor" would otherwise be an empty claim.
 *
 * The rest puts PULSE_IN and PULSE_CAPTURE under mutated pin, level,
 * edge-count and timeout fields and every length (#163). The oracle is written
 * from the wire documentation and the 328P pin table as literals: pins 0..19
 * have a digital port, 20 and 21 (A6/A7) are ADC-only and unsupported, 22 and
 * up are beyond the board; a level is 0 or 1; a timeout is 1..50000 us
 * (TFW_PULSE_MAX_US); a capture asks for 1..31 edges, which is what a 64-byte
 * reply can carry after its count.
 *
 * "Never reaches the HAL" is observed by arming the mock to answer
 * TFW_ERR_TIMEOUT: a refused request still answers its own refusal. "Never
 * touches a pin" is a snapshot of every pin's direction, level and pull-up
 * taken before and compared after. The mock clock must not move: a pulse
 * command is bounded by its timeout, never by the millisecond tick.
 *
 * The random pass uses a fixed seed and names it, with the case number, in
 * every failure message.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "pulse.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "board_profile.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

#define PIN_COUNT_328P  22u
#define FIRST_ADC_ONLY  20u
#define MAX_US          50000u
#define CAPTURE_MAX     31u
#define SEED            0x163A5EEDu
#define RANDOM_CASES    40000u

static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint32_t     rng;

static const uint16_t PORTS[3] = { TFW_ADDR_PINB, TFW_ADDR_PINC, TFW_ADDR_PIND };

void setUp(void)    { tfw_mock_reset(); tfw_mock_set_millis(0x12345678u); }
void tearDown(void) {}

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    memset(outbuf, 0xA5, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t pulse_in(uint8_t pin, uint8_t level, uint32_t timeout)
{
    uint8_t p[6] = { pin, level, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(TFW_CMD_PULSE_IN, p, 6u);
}

static tfw_status_t capture(uint8_t pin, uint8_t start, uint8_t max, uint32_t timeout)
{
    uint8_t p[7] = { pin, start, max, (uint8_t)timeout, (uint8_t)(timeout >> 8),
                     (uint8_t)(timeout >> 16), (uint8_t)(timeout >> 24) };
    return run(TFW_CMD_PULSE_CAPTURE, p, 7u);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void snapshot(uint8_t *s)
{
    for (uint8_t i = 0u; i < 3u; i++) {
        for (uint8_t b = 0u; b < 8u; b++) {
            *s++ = tfw_mock_pin_is_output(PORTS[i], b);
            *s++ = tfw_mock_pin_level(PORTS[i], b);
            *s++ = tfw_mock_pin_pullup(PORTS[i], b);
        }
    }
}

static tfw_status_t pin_status(uint8_t pin)
{
    if (pin >= PIN_COUNT_328P) { return TFW_ERR_BAD_PIN; }
    if (pin >= FIRST_ADC_ONLY) { return TFW_ERR_UNSUPPORTED; }
    return TFW_OK;
}

static int timeout_ok(uint32_t t) { return t != 0u && t <= MAX_US; }

/* The refusal a PULSE_IN request earns before the HAL, or TFW_OK. */
static tfw_status_t in_refusal(const uint8_t *p)
{
    tfw_status_t st = pin_status(p[0]);

    if (st != TFW_OK) { return st; }
    if (p[1] > 1u)    { return TFW_ERR_RANGE; }
    if (!timeout_ok(get_u32(p + 2))) { return TFW_ERR_RANGE; }
    return TFW_OK;
}

static tfw_status_t cap_refusal(const uint8_t *p)
{
    tfw_status_t st = pin_status(p[0]);

    if (st != TFW_OK) { return st; }
    if (p[1] > 1u)    { return TFW_ERR_RANGE; }
    if (p[2] == 0u || p[2] > CAPTURE_MAX) { return TFW_ERR_RANGE; }
    if (!timeout_ok(get_u32(p + 3))) { return TFW_ERR_RANGE; }
    return TFW_OK;
}

/* One PULSE_IN against the oracle, with a pulse of width us on the line
 * (0 = silent). The pins and the clock are unchanged afterwards. */
static void check_in(const uint8_t *p, uint32_t us, const char *msg)
{
    uint8_t      before[72], after[72];
    tfw_status_t exp = in_refusal(p);

    if (exp == TFW_OK && (us == 0u || us > get_u32(p + 2))) {
        exp = TFW_ERR_TIMEOUT;
    }
    tfw_mock_set_pulse_in(us);
    snapshot(before);
    TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_PULSE_IN, p, 6u), msg);
    snapshot(after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof before, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x12345678u, tfw_hal_millis(), msg);
    if (exp == TFW_OK) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(4u, tfw_writer_len(&w), msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(us, get_u32(outbuf), msg);
    } else {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    }
}

/* One PULSE_CAPTURE against the oracle, with n intervals on the line. */
static void check_cap(const uint8_t *p, const uint16_t *iv, uint8_t n, const char *msg)
{
    uint8_t      before[72], after[72];
    tfw_status_t exp = cap_refusal(p);
    uint8_t      got = (uint8_t)(n < p[2] ? n : p[2]);

    if (exp == TFW_OK && got == 0u) {
        exp = TFW_ERR_TIMEOUT;
    }
    tfw_mock_set_capture(iv, n);
    snapshot(before);
    TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_PULSE_CAPTURE, p, 7u), msg);
    snapshot(after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof before, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x12345678u, tfw_hal_millis(), msg);
    if (exp == TFW_OK) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(1u + 2u * got), tfw_writer_len(&w), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(got, outbuf[0], msg);
        for (uint8_t i = 0u; i < got; i++) {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(iv[i],
                (uint16_t)(outbuf[1u + 2u * i] | (outbuf[2u + 2u * i] << 8)), msg);
        }
    } else {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    }
}

static const uint32_t TIMEOUTS[] = {
    0u, 1u, 2u, 26u, 1000u, MAX_US - 1u, MAX_US, MAX_US + 1u,
    0x0000FFFFu, 0x00010000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
};
#define N_TIMEOUTS (sizeof TIMEOUTS / sizeof TIMEOUTS[0])

static void check_in_pin_level(unsigned pin, unsigned lv)
{
    uint8_t p[6];
    char    msg[64];

    p[0] = (uint8_t)pin; p[1] = (uint8_t)lv;
    put_u32(p + 2, 1000u);
    snprintf(msg, sizeof msg, "pin %u level %u", pin, lv);
    check_in(p, 700u, msg);
    check_in(p, 0u, msg);
}

static void test_pulse_in_returns_the_measured_width(void)
{
    tfw_mock_set_pulse_in(1234u);
    TEST_ASSERT_EQUAL(TFW_OK, pulse_in(2u, 1u, 50000u));
    uint32_t us = (uint32_t)outbuf[0] | ((uint32_t)outbuf[1] << 8)
                | ((uint32_t)outbuf[2] << 16) | ((uint32_t)outbuf[3] << 24);
    TEST_ASSERT_EQUAL_UINT32(1234u, us);
}

/*
 * THE DHT22 CASE, which is why this primitive exists.
 *
 * A DHT22 sends 40 bits as pulses of about 26 us for a zero and 70 us for a
 * one. The device captures intervals and knows nothing about humidity; the
 * host turns them into bits. The same command reads HC-SR04, IR and RC PPM.
 */
static void test_capture_returns_the_intervals_a_host_turns_into_bits(void)
{
    const uint16_t dht22[8] = { 26u, 70u, 26u, 26u, 70u, 70u, 26u, 70u };
    tfw_mock_set_capture(dht22, 8u);

    TEST_ASSERT_EQUAL(TFW_OK, capture(9u, 1u, 8u, 50000u));
    TEST_ASSERT_EQUAL_UINT8(8u, outbuf[0]);
    for (uint8_t i = 0u; i < 8u; i++) {
        uint16_t us = (uint16_t)((uint16_t)outbuf[1u + i * 2u]
                                 | ((uint16_t)outbuf[2u + i * 2u] << 8));
        TEST_ASSERT_EQUAL_UINT16(dht22[i], us);
    }
    /* and the host's decode: short is 0, long is 1 */
    uint8_t bits = 0u;
    for (uint8_t i = 0u; i < 8u; i++) {
        uint16_t us = (uint16_t)((uint16_t)outbuf[1u + i * 2u]
                                 | ((uint16_t)outbuf[2u + i * 2u] << 8));
        bits = (uint8_t)(((unsigned)bits << 1) | (us > 48u ? 1u : 0u));
    }
    TEST_ASSERT_EQUAL_HEX8(0x4Du, bits);   /* 0100 1101 */
}

/* Every pin byte against the level boundary, and every level byte on a
 * digital pin, an ADC-only pin and a pin beyond the board, at an in-range
 * timeout, with a pulse on the line and with a silent line. */
static void test_pulse_in_every_pin_and_level(void)
{
    static const uint8_t levels[] = { 0u, 1u, 2u, 0x80u, 0xFFu };
    static const uint8_t pins[]   = { 2u, 20u, 99u };

    for (unsigned pin = 0u; pin < 256u; pin++) {
        for (size_t l = 0u; l < sizeof levels; l++) {
            check_in_pin_level(pin, levels[l]);
        }
    }
    for (size_t k = 0u; k < sizeof pins; k++) {
        for (unsigned lv = 0u; lv < 256u; lv++) {
            check_in_pin_level(pins[k], lv);
        }
    }
}

/* Every timeout boundary against widths on and around it: the width is
 * returned only when it is no longer than the caller's timeout, and a timeout
 * outside 1..50000 is refused whatever the line does. */
static void test_pulse_in_timeout_bound(void)
{
    static const uint32_t widths[] = { 1u, 2u, 26u, 999u, 1000u, 1001u,
                                       MAX_US - 1u, MAX_US, MAX_US + 1u, 0xFFFFFFFFu };
    uint8_t p[6] = { 2u, 1u };
    char    msg[64];

    for (size_t t = 0u; t < N_TIMEOUTS; t++) {
        for (size_t k = 0u; k < sizeof widths / sizeof widths[0]; k++) {
            put_u32(p + 2, TIMEOUTS[t]);
            snprintf(msg, sizeof msg, "timeout %lu width %lu",
                     (unsigned long)TIMEOUTS[t], (unsigned long)widths[k]);
            check_in(p, widths[k], msg);
        }
    }
}

/* Each timeout byte through all 256 values with the others fixed: a byte
 * order mistake moves a value across the bound and shows here. */
static void test_pulse_in_every_timeout_byte(void)
{
    uint8_t p[6];
    char    msg[64];

    for (unsigned i = 0u; i < 4u; i++) {
        for (unsigned v = 0u; v < 256u; v++) {
            p[0] = 7u; p[1] = 0u;
            put_u32(p + 2, 0x00000100u);
            p[2u + i] = (uint8_t)v;
            snprintf(msg, sizeof msg, "timeout byte %u = %u", i, v);
            check_in(p, 0x80u, msg);
        }
    }
}

/* Every pin byte, every start level, every edge count, at in-range and
 * out-of-range timeouts. */
static void test_capture_every_pin_start_and_count(void)
{
    /* the mock holds 32 intervals: one more than a reply can carry */
    static const uint16_t iv[CAPTURE_MAX + 1u] = {
        26u, 70u, 26u, 26u, 70u, 70u, 26u, 70u, 1u, 2u, 3u, 0xFFFFu, 0u,
        500u, 501u, 502u, 503u, 504u, 505u, 506u, 507u, 508u, 509u, 510u,
        511u, 512u, 513u, 514u, 515u, 516u, 517u, 518u,
    };
    static const uint32_t tos[] = { 0u, 1u, MAX_US, MAX_US + 1u, 0xFFFFFFFFu };
    static const uint8_t  maxes[] = { 0u, 1u, 2u, 8u, 30u, 31u, 32u, 33u, 64u, 127u, 128u, 255u };
    uint8_t p[7];
    char    msg[64];

    for (unsigned pin = 0u; pin < 256u; pin++) {
        for (unsigned st = 0u; st < 4u; st++) {
            for (size_t m = 0u; m < sizeof maxes; m++) {
                for (size_t t = 0u; t < sizeof tos / sizeof tos[0]; t++) {
                    unsigned mx = maxes[m];

                    p[0] = (uint8_t)pin;
                    p[1] = (uint8_t)(st < 2u ? st : st * 85u);
                    p[2] = (uint8_t)mx;
                    put_u32(p + 3, tos[t]);
                    snprintf(msg, sizeof msg, "pin %u start %u max %u timeout %lu",
                             pin, p[1], mx, (unsigned long)tos[t]);
                    check_cap(p, iv, (uint8_t)(sizeof iv / sizeof iv[0]), msg);
                }
            }
        }
    }
}

/* A capture with fewer intervals than asked returns what it got -- a sensor
 * that stops after forty bits has said everything it had to say -- and a
 * silent line is a timeout; a refused request is refused before either. */
static void test_capture_short_and_silent_lines(void)
{
    static const uint16_t iv[CAPTURE_MAX] = { 10u, 20u, 30u, 40u, 50u };
    uint8_t p[7];
    char    msg[64];

    for (unsigned have = 0u; have <= CAPTURE_MAX; have++) {
        for (unsigned mx = 0u; mx <= CAPTURE_MAX + 1u; mx++) {
            p[0] = 9u; p[1] = 1u; p[2] = (uint8_t)mx;
            put_u32(p + 3, 20000u);
            snprintf(msg, sizeof msg, "have %u max %u", have, mx);
            check_cap(p, iv, (uint8_t)have, msg);
        }
    }
}

/* Every length other than the command's own is refused by arity, with the HAL
 * armed to answer and the pins untouched. */
static void test_every_other_length_is_refused(void)
{
    uint8_t p[TFW_MAX_PAYLOAD + 1u];
    uint8_t before[72], after[72];
    char    msg[64];

    tfw_mock_set_pulse_in(5u);
    tfw_mock_set_capture((const uint16_t[]){ 1u, 2u }, 2u);
    memset(p, 0, sizeof p);
    p[0] = 2u; p[1] = 1u; p[2] = 2u; p[3] = 0x10u; p[4] = 0x27u;
    for (unsigned n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
        snprintf(msg, sizeof msg, "length %u", n);
        snapshot(before);
        if (n != 6u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(TFW_CMD_PULSE_IN, p, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
        }
        if (n != 7u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(TFW_CMD_PULSE_CAPTURE, p, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
        }
        snapshot(after);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof before, msg);
    }
}

/* Random requests of every field at once, against the same oracle. */
static void test_random_requests_with_a_fixed_seed(void)
{
    static const uint16_t iv[CAPTURE_MAX] = { 3u, 1u, 4u, 1u, 5u, 9u, 2u, 6u };
    uint8_t p[7];
    char    msg[64];

    rng = SEED;
    for (uint32_t i = 0u; i < RANDOM_CASES; i++) {
        uint32_t x = rnd(), y = rnd();

        p[0] = (uint8_t)(x & 0x1Fu);           /* mostly on the board */
        if (x & 0x100u) { p[0] = (uint8_t)(x >> 24); }
        p[1] = (uint8_t)((x >> 9) & 0x3u);     /* 0..3: half out of range */
        p[2] = (uint8_t)((x >> 11) & 0x3Fu);   /* 0..63 edges */
        put_u32(p + 3, (x & 0x200u) ? y : (y % (MAX_US + 2u)));
        snprintf(msg, sizeof msg, "seed 0x%08lx case %lu",
                 (unsigned long)SEED, (unsigned long)i);
        check_cap(p, iv, (uint8_t)((y >> 8) & 0x1Fu), msg);
        p[2] = p[1]; p[1] = p[0];
        check_in(p + 1, (y >> 16) & 0xFFFFu, msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pulse_in_returns_the_measured_width);
    RUN_TEST(test_capture_returns_the_intervals_a_host_turns_into_bits);
    RUN_TEST(test_pulse_in_every_pin_and_level);
    RUN_TEST(test_pulse_in_timeout_bound);
    RUN_TEST(test_pulse_in_every_timeout_byte);
    RUN_TEST(test_capture_every_pin_start_and_count);
    RUN_TEST(test_capture_short_and_silent_lines);
    RUN_TEST(test_every_other_length_is_refused);
    RUN_TEST(test_random_requests_with_a_fixed_seed);
    return UNITY_END();
}
