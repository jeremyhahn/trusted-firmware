/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the pulse, analog, PWM and tone commands.
 *
 * test_pulse, test_analog and test_pwm pin what a clean request does. This
 * feeds the same requests, through the same dispatcher, mutated: bit flips,
 * truncation at every length, all 0x00, all 0xFF, random bytes, and every pin,
 * level, count, duty, frequency and timeout swept through 0, 1, the maximum,
 * one past it and wrapped. Each payload sits in a heap buffer of exactly its
 * length; each reply goes to a writer of a chosen capacity, some bytes already
 * in it, with guard bytes after it. Every call is held to:
 *
 *   - nothing past the writer's capacity, nothing over the bytes it held;
 *   - a refusal leaves the writer's length where it was;
 *   - a reading answers exactly what the mock measured, never more edges than
 *     the request asked for or a frame can carry;
 *   - a reserved pin never drives PWM or a tone, and a refused tone is silent.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the command and the payload so the case can be pinned.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "analog.h"
#include "pulse.h"
#include "pwm.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define GUARD        16u
#define GUARD_BYTE   0xA5u
#define PRE_BYTE     0xC3u
#define LIMIT        (TFW_MAX_PAYLOAD + 1u)
#define CAPTURE_MAX  ((TFW_MAX_PAYLOAD - 1u) / 2u) /* TFW_CAPTURE_MAX, private to src/caps/pulse.c */
#define PULSE_US     1234u
#define TONE_FLOOR   490u                          /* the mock's lowest reachable tone */
#define RANDOM_RUNS  1000u                         /* per command */

static uint32_t rng;
static uint16_t edges[CAPTURE_MAX];
static unsigned ran;
static unsigned ok_runs;
static unsigned refused_runs;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint16_t adc_value(uint8_t ch)
{
    return (uint16_t)(ch * 100u + 7u);
}

void setUp(void)
{
    unsigned i;

    tfw_mock_reset();
    for (i = 0u; i < CAPTURE_MAX; i++) {
        edges[i] = (uint16_t)(i * 37u + 11u);
    }
    tfw_mock_set_capture(edges, (uint8_t)CAPTURE_MAX);
    tfw_mock_set_pulse_in(PULSE_US);
    for (i = 0u; i < 8u; i++) {
        tfw_mock_set_adc((uint8_t)i, adc_value((uint8_t)i));
    }
}
void tearDown(void) {}

static void start(uint32_t seed)
{
    rng          = seed;
    ran          = 0u;
    ok_runs      = 0u;
    refused_runs = 0u;
}

static void report(const char *what)
{
    printf("%s: %u requests, %u ok, %u refused\n", what, ran, ok_runs, refused_runs);
}

static void dump(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    unsigned i;
    printf("cmd 0x%02x cap %u pre %u payload[%u]:", cmd, cap, pre, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static uint8_t reserved_pin(uint8_t pin)
{
    return (uint8_t)((tfw_board_pin_flags(pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) != 0u);
}

static uint8_t board_pin(uint8_t pin)
{
    tfw_pinref_t ref;
    return (uint8_t)(tfw_board_pin(pin, &ref) == TFW_OK);
}

static uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t timeout_ok(uint32_t t)
{
    return (uint8_t)(t != 0u && t <= TFW_PULSE_MAX_US);
}

/* A reply of exactly want bytes, or TOO_LONG because they did not fit. */
static const char *check_len(tfw_status_t st, unsigned rlen, unsigned room, unsigned want)
{
    if (st == TFW_ERR_TOO_LONG) {
        return want > room ? NULL : "a reply that fitted was called too long";
    }
    return rlen == want ? NULL : "a reply of the wrong length";
}

/* What the call must have done, given the request and its status; NULL when
 * it did. */
static const char *check(uint8_t cmd, const uint8_t *p, unsigned n, tfw_status_t st,
                         const uint8_t *reply, unsigned rlen, unsigned room)
{
    const uint8_t taken = (uint8_t)(st == TFW_OK || st == TFW_ERR_TOO_LONG);
    const char   *fail;
    unsigned      i;

    if (cmd != TFW_CMD_TONE && tfw_mock_tone_hz() != 0u) {
        return "a command that is not TONE started a tone";
    }
    if (!taken) {
        if (cmd == TFW_CMD_TONE && tfw_mock_tone_hz() != 0u) {
            return "a refused tone sounded";
        }
        return NULL;
    }
    switch (cmd) {
    case TFW_CMD_ANALOG_READ: {
        uint8_t ch;
        if (n != 1u || tfw_board_adc_channel(p[0], &ch) != TFW_OK) {
            return "an analog read outside the contract was accepted";
        }
        if ((fail = check_len(st, rlen, room, 2u)) != NULL || st != TFW_OK) {
            return fail;
        }
        return (uint16_t)(reply[0] | (reply[1] << 8)) == adc_value(ch)
               ? NULL : "an analog read answered another channel";
    }
    case TFW_CMD_ADC_REF:
        if (n != 1u || p[0] > (uint8_t)TFW_ADC_REF_INTERNAL) {
            return "an ADC reference outside the contract was accepted";
        }
        return rlen ? "ADC_REF answered bytes" : NULL;
    case TFW_CMD_PWM_WRITE: {
        tfw_pwmref_t ref;
        if (n != 2u || reserved_pin(p[0]) || tfw_board_pwm(p[0], &ref) != TFW_OK) {
            return "a PWM write outside the contract was accepted";
        }
        return rlen ? "PWM_WRITE answered bytes" : NULL;
    }
    case TFW_CMD_TONE: {
        const uint16_t hz = (uint16_t)(p[1] | (p[2] << 8));
        if (n != 3u || p[0] != TFW_TONE_PIN || reserved_pin(p[0])
            || (hz != 0u && hz < TONE_FLOOR) || tfw_mock_tone_hz() != hz) {
            return "a tone outside the contract was accepted";
        }
        return rlen ? "TONE answered bytes" : NULL;
    }
    case TFW_CMD_NO_TONE:
        if (n > 1u) {
            return "a NO_TONE past its arity was accepted";
        }
        return rlen ? "NO_TONE answered bytes" : NULL;
    case TFW_CMD_PULSE_IN:
        if (n != 6u || !board_pin(p[0]) || p[1] > 1u || !timeout_ok(u32_at(&p[2]))
            || u32_at(&p[2]) < PULSE_US) {
            return "a pulse measurement outside the contract was accepted";
        }
        if ((fail = check_len(st, rlen, room, 4u)) != NULL || st != TFW_OK) {
            return fail;
        }
        return u32_at(reply) == PULSE_US ? NULL : "a pulse measurement answered another width";
    case TFW_CMD_PULSE_CAPTURE: {
        const unsigned want = p[2] < CAPTURE_MAX ? p[2] : CAPTURE_MAX;
        if (n != 7u || !board_pin(p[0]) || p[1] > 1u || p[2] == 0u || p[2] > CAPTURE_MAX
            || !timeout_ok(u32_at(&p[3]))) {
            return "a capture outside the contract was accepted";
        }
        if ((fail = check_len(st, rlen, room, 1u + want * 2u)) != NULL || st != TFW_OK) {
            return fail;
        }
        if (reply[0] != want) {
            return "a capture answered a different edge count";
        }
        for (i = 0u; i < want; i++) {
            if ((uint16_t)(reply[1u + i * 2u] | (reply[2u + i * 2u] << 8)) != edges[i]) {
                return "a capture answered an interval the line did not give";
            }
        }
        return NULL;
    }
    default:
        return "an unexpected command";
    }
}

static tfw_status_t feed_at(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(cap + GUARD);
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    const char   *fail = NULL;
    unsigned      len;
    unsigned      i;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, p, n); }
    memset(out, GUARD_BYTE, cap + GUARD);
    setUp();

    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PRE_BYTE);
    }
    st  = tfw_dispatch(cmd, &r, &w);
    len = tfw_writer_len(&w);

    for (i = cap; i < cap + GUARD && !fail; i++) {
        if (out[i] != GUARD_BYTE) {
            fail = "wrote past the writer's capacity";
        }
    }
    for (i = 0u; i < pre && !fail; i++) {
        if (out[i] != PRE_BYTE) {
            fail = "overwrote bytes the writer already held";
        }
    }
    if (!fail && len > cap) {
        fail = "the writer's length ran past its capacity";
    }
    if (!fail && (unsigned)st > (unsigned)TFW_ERR_NO_STORE) {
        fail = "an unknown status";
    }
    if (!fail && st != TFW_OK && st != TFW_ERR_TOO_LONG && len != pre) {
        fail = "a refused request moved the writer";
    }
    if (!fail && n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC) {
        fail = "a request past the frame maximum was not refused";
    }
    if (!fail) {
        fail = check(cmd, p, n, st, out + pre, len - pre, cap - pre);
    }
    if (fail) {
        printf("status %u, writer %u\n", (unsigned)st, len);
        dump(cmd, p, n, cap, pre);
        TEST_FAIL_MESSAGE(fail);
    }
    ran++;
    if (st == TFW_OK) {
        ok_runs++;
    } else {
        refused_runs++;
    }
    free(in);
    free(out);
    return st;
}

/* Each request twice: into a whole reply buffer, then into a random shorter
 * one that already holds a byte or two. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *p, unsigned n)
{
    const tfw_status_t st  = feed_at(cmd, p, n, TFW_MAX_PAYLOAD, 0u);
    const unsigned     cap = rnd() % (TFW_MAX_PAYLOAD + 1u);
    (void)feed_at(cmd, p, n, cap, cap ? rnd() % (cap < 3u ? cap + 1u : 3u) : 0u);
    return st;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The seeds are the requests test_pulse, test_analog and test_pwm send. */
static const uint8_t pulse_in_seed[6] = { 2u, 1u, 0x50u, 0xC3u, 0x00u, 0x00u };      /* 50000 us */
static const uint8_t capture_seed[7]  = { 9u, 1u, 8u, 0x50u, 0xC3u, 0x00u, 0x00u };
static const uint8_t analog_seed[1]   = { 14u };
static const uint8_t adc_ref_seed[1]  = { 2u };
static const uint8_t pwm_seed[2]      = { 9u, 128u };
static const uint8_t tone_seed[3]     = { TFW_TONE_PIN, 0x10u, 0x27u };             /* 10000 Hz */
static const uint8_t no_tone_seed[1]  = { TFW_TONE_PIN };

/* The values a timeout goes wrong at: zero, the measured width, the cap, and
 * the wrap. */
static const uint32_t timeouts[] = {
    0u, 1u, PULSE_US - 1u, PULSE_US, PULSE_US + 1u, TFW_PULSE_MAX_US - 1u, TFW_PULSE_MAX_US,
    TFW_PULSE_MAX_US + 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu,
};
#define NTIMEOUTS (sizeof timeouts / sizeof timeouts[0])

static const uint8_t levels[] = { 0u, 1u, 2u, 0xFFu };

static void test_every_pin_against_every_single_byte_command(void)
{
    static const uint8_t duties[] = { 0u, 1u, 127u, 128u, 254u, 255u };
    uint8_t  p[2];
    unsigned pin;
    unsigned k;

    start(0x9E3779B9u);
    for (pin = 0u; pin < 256u; pin++) {
        p[0] = (uint8_t)pin;
        feed(TFW_CMD_ANALOG_READ, p, 1u);
        feed(TFW_CMD_ADC_REF, p, 1u);
        feed(TFW_CMD_NO_TONE, p, 1u);
        for (k = 0u; k < sizeof duties; k++) {
            p[1] = duties[k];
            feed(TFW_CMD_PWM_WRITE, p, 2u);
        }
    }
    feed(TFW_CMD_NO_TONE, p, 0u);
    TEST_ASSERT_EQUAL_UINT(2u * (256u * (3u + sizeof duties) + 1u), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("analog, adc ref, pwm, no tone fields");
}

static void test_every_pin_against_every_tone_edge(void)
{
    static const uint16_t hz[] = {
        0u, 1u, TONE_FLOOR - 1u, TONE_FLOOR, TONE_FLOOR + 1u, 10000u, 0x7FFFu, 0x8000u,
        0xFFFEu, 0xFFFFu,
    };
    uint8_t  p[3];
    unsigned pin;
    unsigned k;

    start(0x7F4A7C15u);
    for (pin = 0u; pin < 256u; pin++) {
        for (k = 0u; k < sizeof hz / sizeof hz[0]; k++) {
            p[0] = (uint8_t)pin;
            p[1] = (uint8_t)hz[k];
            p[2] = (uint8_t)(hz[k] >> 8);
            feed(TFW_CMD_TONE, p, 3u);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * (sizeof hz / sizeof hz[0]), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("tone fields");
}

static void test_every_pin_level_and_timeout_of_a_pulse(void)
{
    uint8_t  p[6];
    unsigned pin;
    unsigned l;
    unsigned t;

    start(0x2545F491u);
    for (pin = 0u; pin < 256u; pin++) {
        for (l = 0u; l < sizeof levels; l++) {
            for (t = 0u; t < NTIMEOUTS; t++) {
                p[0] = (uint8_t)pin;
                p[1] = levels[l];
                put_u32(&p[2], timeouts[t]);
                feed(TFW_CMD_PULSE_IN, p, 6u);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * sizeof levels * NTIMEOUTS, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("pulse in fields");
}

static void test_every_pin_level_count_and_timeout_of_a_capture(void)
{
    static const uint8_t counts[] = {
        0u, 1u, 8u, CAPTURE_MAX - 1u, CAPTURE_MAX, CAPTURE_MAX + 1u, CAPTURE_MAX + 2u,
        0x7Fu, 0x80u, 0xFFu,
    };
    static const uint32_t short_timeouts[] = {
        0u, 1u, TFW_PULSE_MAX_US, TFW_PULSE_MAX_US + 1u, 0xFFFFFFFFu,
    };
    uint8_t  p[7];
    unsigned pin;
    unsigned l;
    unsigned c;
    unsigned t;

    start(0x6C8E9CF5u);
    for (pin = 0u; pin < 256u; pin++) {
        for (l = 0u; l < sizeof levels; l++) {
            for (c = 0u; c < sizeof counts; c++) {
                for (t = 0u; t < sizeof short_timeouts / sizeof short_timeouts[0]; t++) {
                    p[0] = (uint8_t)pin;
                    p[1] = levels[l];
                    p[2] = counts[c];
                    put_u32(&p[3], short_timeouts[t]);
                    feed(TFW_CMD_PULSE_CAPTURE, p, 7u);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u * sizeof levels * sizeof counts
                           * (sizeof short_timeouts / sizeof short_timeouts[0]), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("pulse capture fields");
}

/* Every mutation of one seed; returns the number of requests it fed. */
static unsigned mutate(uint8_t cmd, const uint8_t *seed, unsigned sn)
{
    uint8_t  buf[LIMIT];
    unsigned n;
    unsigned i;
    unsigned k;
    unsigned b;

    /* truncation at every length, and every length up to one past the frame */
    for (n = 0u; n <= LIMIT; n++) {
        memcpy(buf, seed, n < sn ? n : sn);
        if (n > sn) {
            random_fill(buf + sn, n - sn);
        }
        feed(cmd, buf, n);
    }
    /* all 0x00 and all 0xFF at every length */
    for (n = 0u; n <= LIMIT; n++) {
        memset(buf, 0x00, n);
        feed(cmd, buf, n);
        memset(buf, 0xFF, n);
        feed(cmd, buf, n);
    }
    /* every single bit flipped */
    for (b = 0u; b < sn * 8u; b++) {
        memcpy(buf, seed, sn);
        buf[b / 8u] ^= (uint8_t)(1u << (b % 8u));
        feed(cmd, buf, sn);
    }
    /* several bits, then a random cut or a random tail */
    for (k = 0u; k < RANDOM_RUNS; k++) {
        unsigned flips = 1u + rnd() % 4u;
        n = sn;
        memcpy(buf, seed, sn);
        while (flips--) {
            const uint32_t at = rnd() % (n * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        switch (rnd() % 3u) {
        case 0u:
            n = rnd() % (n + 1u);
            break;
        case 1u:
            i = rnd() % (LIMIT - n + 1u);
            random_fill(buf + n, i);
            n += i;
            break;
        default:
            break;
        }
        feed(cmd, buf, n);
    }
    /* pure random payloads of every length */
    for (n = 0u; n <= LIMIT; n++) {
        for (k = 0u; k < 4u; k++) {
            random_fill(buf, n);
            feed(cmd, buf, n);
        }
    }
    return 2u * ((LIMIT + 1u) * 3u + sn * 8u + RANDOM_RUNS + (LIMIT + 1u) * 4u);
}

static void test_mutated_pin_requests(void)
{
    unsigned want = 0u;

    start(0x85EBCA6Bu);
    want += mutate(TFW_CMD_PULSE_IN, pulse_in_seed, sizeof pulse_in_seed);
    want += mutate(TFW_CMD_PULSE_CAPTURE, capture_seed, sizeof capture_seed);
    want += mutate(TFW_CMD_ANALOG_READ, analog_seed, sizeof analog_seed);
    want += mutate(TFW_CMD_ADC_REF, adc_ref_seed, sizeof adc_ref_seed);
    want += mutate(TFW_CMD_PWM_WRITE, pwm_seed, sizeof pwm_seed);
    want += mutate(TFW_CMD_TONE, tone_seed, sizeof tone_seed);
    want += mutate(TFW_CMD_NO_TONE, no_tone_seed, sizeof no_tone_seed);
    TEST_ASSERT_EQUAL_UINT(want, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("mutated pin requests");
}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n, uint8_t *out)
{
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, p, n);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    return tfw_dispatch(cmd, &r, &w);
}

/* After all that, the clean requests answer as the existing tests pin them. */
static void test_the_clean_requests_afterwards(void)
{
    const uint16_t dht22[8] = { 26u, 70u, 26u, 26u, 70u, 70u, 26u, 70u };
    uint8_t        out[TFW_MAX_PAYLOAD];
    uint8_t        i;

    tfw_mock_reset();
    tfw_mock_set_pulse_in(1234u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PULSE_IN, pulse_in_seed, 6u, out));
    TEST_ASSERT_EQUAL_UINT32(1234u, u32_at(out));

    tfw_mock_set_capture(dht22, 8u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PULSE_CAPTURE, capture_seed, 7u, out));
    TEST_ASSERT_EQUAL_UINT8(8u, out[0]);
    for (i = 0u; i < 8u; i++) {
        TEST_ASSERT_EQUAL_UINT16(dht22[i], (uint16_t)(out[1u + i * 2u] | (out[2u + i * 2u] << 8)));
    }

    tfw_mock_set_adc(0u, 512u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ANALOG_READ, analog_seed, 1u, out));
    TEST_ASSERT_EQUAL_UINT16(512u, (uint16_t)(out[0] | (out[1] << 8)));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ADC_REF, adc_ref_seed, 1u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ADC_REF, (const uint8_t[]){ 9u }, 1u, out));

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, pwm_seed, 2u, out));
    TEST_ASSERT_EQUAL_UINT8(128u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
    TEST_ASSERT_TRUE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 1u, 100u }, 2u, out));

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TONE, tone_seed, 3u, out));
    TEST_ASSERT_EQUAL_UINT16(10000u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_NO_TONE, no_tone_seed, 1u, out));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pin_against_every_single_byte_command);
    RUN_TEST(test_every_pin_against_every_tone_edge);
    RUN_TEST(test_every_pin_level_and_timeout_of_a_pulse);
    RUN_TEST(test_every_pin_level_count_and_timeout_of_a_capture);
    RUN_TEST(test_mutated_pin_requests);
    RUN_TEST(test_the_clean_requests_afterwards);
    return UNITY_END();
}
