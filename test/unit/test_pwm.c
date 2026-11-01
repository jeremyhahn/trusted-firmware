/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM_WRITE, TONE and NO_TONE through the dispatcher, under mutated fields and
 * every length (#163).
 *
 * PWM_WRITE is [pin:1][duty:1]; there is no frequency field on the wire, the
 * frequency is the timer's. Its oracle is a model of the four compare units and
 * their pins, written from the 328P table as literals: pins 3, 9, 10 and 11
 * drive OC2B, OC1A, OC1B and OC2A; pins 0 and 1 are the UART and reserved; 5
 * and 6 belong to the tick timer and every other pin below 22 has no unit, both
 * unsupported; 22 and up are beyond the board. A duty of zero disconnects the
 * unit and leaves its compare register alone; any other duty connects it and
 * is written as given. An accepted request makes its pin an output. After every
 * request the mock's units and every pin's direction, level and pull-up are
 * compared with the model, so a refused request that touched anything shows.
 * The model is never reset between requests: state carried across a refusal is
 * part of what is checked.
 *
 * TONE is [pin:1][hz:2 little-endian]; NO_TONE takes an optional pin byte and
 * ignores it. Its oracle is a model of the one tone generator, written as
 * literals: tone plays only on pin 11 (OC2A) and every other pin byte is
 * unsupported, before any range check; a frequency of zero stops it; a
 * frequency below 490 Hz is out of the AVR prescaler's reach, which the mock
 * models, and is refused leaving what was playing alone; a second start retunes
 * the generator. After every request the frequency is compared with the model
 * and every pin's direction, level and pull-up with what it was, for a refusal.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "pwm.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define PIN_COUNT_328P 22u
#define PWM_SEED       0x163B0A7Du
#define TONE_SEED      0x163C70E5u
#define RANDOM_CASES   100000u
#define TONE_PIN       11u
#define MIN_HZ         490u

typedef struct {
    uint8_t  pin;
    uint16_t port;
    uint8_t  bit;
    uint16_t ctl;
    uint16_t duty_key;
    uint8_t  ctl_bit;
} unit_t;

static const unit_t UNITS[4] = {
    { 3u,  TFW_ADDR_PIND, 3u, TFW_ADDR_TCCR2A, TFW_ADDR_OCR2B,  COM2B1_BIT },
    { 9u,  TFW_ADDR_PINB, 1u, TFW_ADDR_TCCR1A, TFW_ADDR_OCR1AL, COM1A1_BIT },
    { 10u, TFW_ADDR_PINB, 2u, TFW_ADDR_TCCR1A, TFW_ADDR_OCR1BL, COM1B1_BIT },
    { 11u, TFW_ADDR_PINB, 3u, TFW_ADDR_TCCR2A, TFW_ADDR_OCR2A,  COM2A1_BIT },
};

static const uint16_t PORTS[3] = { TFW_ADDR_PINB, TFW_ADDR_PINC, TFW_ADDR_PIND };

static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint32_t     rng;

/* the PWM model */
static uint8_t m_connected[4], m_duty[4], m_output[4];
static uint8_t m_pins[72];   /* every pin but the four units' as it was at setUp */

/* the tone model: what is playing, 0 for silence */
static uint16_t m_hz;

void setUp(void)    { tfw_mock_reset(); m_hz = 0u; }
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

static int unit_of(uint8_t pin)
{
    for (int i = 0; i < 4; i++) {
        if (UNITS[i].pin == pin) { return i; }
    }
    return -1;
}

static int is_unit_pin(uint16_t port, uint8_t bit)
{
    for (int i = 0; i < 4; i++) {
        if (UNITS[i].port == port && UNITS[i].bit == bit) { return 1; }
    }
    return 0;
}

static void snapshot_other_pins(uint8_t *s)
{
    for (uint8_t i = 0u; i < 3u; i++) {
        for (uint8_t b = 0u; b < 8u; b++) {
            if (is_unit_pin(PORTS[i], b)) {
                s[0] = s[1] = s[2] = 0xEEu;
            } else {
                s[0] = tfw_mock_pin_is_output(PORTS[i], b);
                s[1] = tfw_mock_pin_level(PORTS[i], b);
                s[2] = tfw_mock_pin_pullup(PORTS[i], b);
            }
            s += 3;
        }
    }
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

static void model_reset(void)
{
    memset(m_connected, 0, sizeof m_connected);
    memset(m_duty, 0, sizeof m_duty);
    memset(m_output, 0, sizeof m_output);
    snapshot_other_pins(m_pins);
}

static tfw_status_t expected_status(uint8_t pin)
{
    if (pin >= PIN_COUNT_328P) { return TFW_ERR_BAD_PIN; }
    if (pin <= 1u)             { return TFW_ERR_RESERVED_PIN; }
    if (unit_of(pin) < 0)      { return TFW_ERR_UNSUPPORTED; }
    return TFW_OK;
}

static void check_model(const char *msg)
{
    uint8_t now[72];

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(m_connected[i],
            tfw_mock_pwm_connected(UNITS[i].ctl, UNITS[i].ctl_bit), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(m_duty[i], tfw_mock_pwm_duty(UNITS[i].duty_key), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(m_output[i],
            tfw_mock_pin_is_output(UNITS[i].port, UNITS[i].bit), msg);
    }
    snapshot_other_pins(now);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(m_pins, now, sizeof now, msg);
}

/* One PWM_WRITE against the model. */
static void step(uint8_t pin, uint8_t duty, const char *msg)
{
    tfw_status_t exp = expected_status(pin);
    uint8_t      p[2] = { pin, duty };

    TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_PWM_WRITE, p, 2u), msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    if (exp == TFW_OK) {
        int u = unit_of(pin);

        m_output[u] = 1u;
        if (duty == 0u) {
            m_connected[u] = 0u;
        } else {
            m_connected[u] = 1u;
            m_duty[u] = duty;
        }
    }
    check_model(msg);
}

/* One TONE against the model. */
static void start(uint8_t pin, uint16_t hz, const char *msg)
{
    uint8_t      p[3] = { pin, (uint8_t)hz, (uint8_t)(hz >> 8) };
    uint8_t      before[72], after[72];
    tfw_status_t exp;

    if (pin != TONE_PIN)            { exp = TFW_ERR_UNSUPPORTED; }
    else if (hz != 0u && hz < MIN_HZ) { exp = TFW_ERR_RANGE; }
    else                            { exp = TFW_OK; m_hz = hz; }

    snapshot(before);
    TEST_ASSERT_EQUAL_MESSAGE(exp, run(TFW_CMD_TONE, p, 3u), msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(m_hz, tfw_mock_tone_hz(), msg);
    if (exp != TFW_OK) {
        snapshot(after);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof before, msg);
    }
}

/* One NO_TONE of length n (0 or 1) against the model. */
static void stop(const uint8_t *p, uint8_t n, const char *msg)
{
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, run(TFW_CMD_NO_TONE, p, n), msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
    m_hz = 0u;
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, tfw_mock_tone_hz(), msg);
}

/* A duty of zero DISCONNECTS the pin rather than setting a zero duty. Holding
 * it low through the timer would quietly prevent digital_write from working on
 * that pin afterwards. */
static void test_a_duty_of_zero_disconnects_rather_than_holding_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 200u }, 2u));
    TEST_ASSERT_TRUE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PWM_WRITE, (const uint8_t[]){ 9u, 0u }, 2u));
    TEST_ASSERT_FALSE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
}

/* Every pin byte against every duty byte, in one run with the state carried
 * through: each unit sees every duty, connected and then disconnected, and
 * every refusal lands on whatever the units were left doing. Pins 5 and 6 HAVE
 * compare units, but Timer0 is the millisecond tick: their refusal is what
 * stops a host writing a duty into a timer counting something else. */
static void test_every_pin_and_duty(void)
{
    char msg[48];

    model_reset();
    for (unsigned d = 0u; d < 256u; d++) {
        for (unsigned pin = 0u; pin < 256u; pin++) {
            snprintf(msg, sizeof msg, "pin %u duty %u", pin, d);
            step((uint8_t)pin, (uint8_t)(d ^ (pin & 1u ? 0xFFu : 0u)), msg);
        }
    }
}

/* The duty bounds on every unit: 0 disconnects, 1 and 255 are taken as
 * given, and disconnecting keeps the last duty in the compare register. */
static void test_duty_bounds_on_every_unit(void)
{
    static const uint8_t duties[] = { 1u, 0u, 255u, 0u, 0u, 128u, 254u, 1u, 0u };
    char msg[48];

    model_reset();
    for (int u = 0; u < 4; u++) {
        for (size_t k = 0u; k < sizeof duties; k++) {
            snprintf(msg, sizeof msg, "unit %d step %u", u, (unsigned)k);
            step(UNITS[u].pin, duties[k], msg);
        }
    }
}

/* Every length other than two is refused by arity, and nothing moves. */
static void test_pwm_every_other_length_is_refused(void)
{
    uint8_t p[TFW_MAX_PAYLOAD + 1u];
    char    msg[48];

    model_reset();
    for (int u = 0; u < 4; u++) {
        memset(p, 0x40, sizeof p);
        p[0] = UNITS[u].pin;
        for (unsigned n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
            if (n == 2u) { continue; }
            snprintf(msg, sizeof msg, "unit %d length %u", u, n);
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(TFW_CMD_PWM_WRITE, p, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
            check_model(msg);
        }
    }
}

/* A digital pin a host has set up stays as it was when PWM on it is refused:
 * the reserved UART pins and the tick timer's pins, driven high first. */
static void test_a_refused_pin_keeps_its_gpio_state(void)
{
    static const uint8_t pins[] = { 2u, 4u, 5u, 6u, 7u, 8u, 12u, 13u, 14u, 19u };
    char msg[48];

    for (size_t k = 0u; k < sizeof pins; k++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PIN_MODE, (const uint8_t[]){ pins[k], 1u }, 2u));
        TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ pins[k], 1u }, 2u));
    }
    model_reset();
    for (size_t k = 0u; k < sizeof pins; k++) {
        for (unsigned d = 0u; d < 256u; d += 51u) {
            snprintf(msg, sizeof msg, "pin %u duty %u", pins[k], d);
            step(pins[k], (uint8_t)d, msg);
        }
    }
    for (uint8_t pin = 0u; pin <= 1u; pin++) {
        snprintf(msg, sizeof msg, "uart pin %u", pin);
        step(pin, 200u, msg);
    }
}

/* Random requests with the state carried through, against the model. */
static void test_pwm_random_requests_with_a_fixed_seed(void)
{
    char msg[48];

    model_reset();
    rng = PWM_SEED;
    for (uint32_t i = 0u; i < RANDOM_CASES; i++) {
        uint32_t x   = rnd();
        uint8_t  pin = (x & 0x3u) ? UNITS[(x >> 2) & 0x3u].pin : (uint8_t)(x >> 8);
        uint8_t  d   = (x & 0x10u) ? 0u : (uint8_t)(x >> 16);

        snprintf(msg, sizeof msg, "seed 0x%08lx case %lu",
                 (unsigned long)PWM_SEED, (unsigned long)i);
        step(pin, d, msg);
    }
}

static void test_tone_starts_and_stops(void)
{
    /* Pin 11. Tone is one timer driving ONE output-compare pin -- OC2A, which
     * is PB3 on this part -- so the pin is not the caller's choice. */
    const uint8_t on[3] = { TFW_TONE_PIN, 0x10u, 0x27u };   /* 10000 Hz, little-endian */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TONE, on, 3u));
    TEST_ASSERT_EQUAL_UINT16(10000u, tfw_mock_tone_hz());

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_NO_TONE, (const uint8_t[]){ TFW_TONE_PIN }, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
}

/* Every frequency on the tone pin, each started over the one before. A
 * frequency below what the prescaler can express is refused rather than
 * producing a tone at some other pitch entirely. */
static void test_every_frequency_on_the_tone_pin(void)
{
    char msg[48];

    for (uint32_t hz = 0u; hz <= 0xFFFFu; hz++) {
        snprintf(msg, sizeof msg, "hz %lu", (unsigned long)hz);
        start(TONE_PIN, (uint16_t)hz, msg);
    }
}

/* Every pin byte at frequencies on each side of the bound, with a tone
 * already playing: no other pin starts, retunes or silences it.
 *
 * Pin 3 matters most: it has a compare unit -- OC2B, on the same Timer2 -- so
 * it is exactly the pin someone reaches for, and exactly the one that would
 * have been accepted and then made no sound. Answering TFW_OK for that is
 * worse than refusing, because the host has no way to discover its speaker is
 * on the wrong leg. */
static void test_every_pin_byte_while_a_tone_plays(void)
{
    static const uint16_t hzs[] = { 0u, 1u, MIN_HZ - 1u, MIN_HZ, 1000u, 0xFFFFu };
    char msg[48];

    start(TONE_PIN, 4000u, "first start");
    for (unsigned pin = 0u; pin < 256u; pin++) {
        if (pin == TONE_PIN) { continue; }
        for (size_t k = 0u; k < sizeof hzs / sizeof hzs[0]; k++) {
            snprintf(msg, sizeof msg, "pin %u hz %u", pin, hzs[k]);
            start((uint8_t)pin, hzs[k], msg);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(4000u, tfw_mock_tone_hz());
}

/* Start, a second start on the busy pin, a refused start, stop, start again. */
static void test_start_restart_stop_on_a_busy_pin(void)
{
    start(TONE_PIN, 1000u, "start");
    start(TONE_PIN, 2000u, "second start retunes");
    start(TONE_PIN, 100u, "a refused start keeps 2000");
    TEST_ASSERT_EQUAL_UINT16(2000u, tfw_mock_tone_hz());
    start(TONE_PIN, 2000u, "the same frequency again");
    stop(NULL, 0u, "stop");
    stop(NULL, 0u, "a second stop on silence");
    start(TONE_PIN, MIN_HZ, "start after stop");
    start(TONE_PIN, 0u, "zero stops");
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
}

/* NO_TONE takes no byte or any one byte, and silences whatever plays. */
static void test_no_tone_with_every_pin_byte(void)
{
    char msg[48];

    for (unsigned b = 0u; b < 256u; b++) {
        uint8_t pin = (uint8_t)b;

        start(TONE_PIN, (uint16_t)(MIN_HZ + b), "start");
        snprintf(msg, sizeof msg, "no_tone pin %u", b);
        stop(&pin, 1u, msg);
    }
}

/* Every other length is refused by arity, and what plays keeps playing. */
static void test_tone_every_other_length_is_refused(void)
{
    uint8_t p[TFW_MAX_PAYLOAD + 1u];
    char    msg[48];

    memset(p, 0x30, sizeof p);
    p[0] = TONE_PIN;
    start(TONE_PIN, 3000u, "start");
    for (unsigned n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
        snprintf(msg, sizeof msg, "length %u", n);
        if (n != 3u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(TFW_CMD_TONE, p, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000u, tfw_mock_tone_hz(), msg);
        }
        if (n > 1u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(TFW_CMD_NO_TONE, p, (uint8_t)n), msg);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(3000u, tfw_mock_tone_hz(), msg);
        }
    }
}

/* Random starts and stops against the model. */
static void test_tone_random_requests_with_a_fixed_seed(void)
{
    char msg[48];

    rng = TONE_SEED;
    for (uint32_t i = 0u; i < RANDOM_CASES; i++) {
        uint32_t x   = rnd();
        uint8_t  pin = (x & 0x7u) ? TONE_PIN : (uint8_t)(x >> 24);

        snprintf(msg, sizeof msg, "seed 0x%08lx case %lu",
                 (unsigned long)TONE_SEED, (unsigned long)i);
        if ((x & 0x18u) == 0u) {
            stop(&pin, (uint8_t)((x >> 5) & 1u), msg);
        } else {
            start(pin, (uint16_t)(x >> 8), msg);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_duty_of_zero_disconnects_rather_than_holding_low);
    RUN_TEST(test_every_pin_and_duty);
    RUN_TEST(test_duty_bounds_on_every_unit);
    RUN_TEST(test_pwm_every_other_length_is_refused);
    RUN_TEST(test_a_refused_pin_keeps_its_gpio_state);
    RUN_TEST(test_pwm_random_requests_with_a_fixed_seed);
    RUN_TEST(test_tone_starts_and_stops);
    RUN_TEST(test_every_frequency_on_the_tone_pin);
    RUN_TEST(test_every_pin_byte_while_a_tone_plays);
    RUN_TEST(test_start_restart_stop_on_a_busy_pin);
    RUN_TEST(test_no_tone_with_every_pin_byte);
    RUN_TEST(test_tone_every_other_length_is_refused);
    RUN_TEST(test_tone_random_requests_with_a_fixed_seed);
    return UNITY_END();
}
