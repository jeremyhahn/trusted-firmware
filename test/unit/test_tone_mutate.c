/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TONE and NO_TONE under mutated pin and frequency fields and every length
 * (#163). TONE is [pin:1][hz:2 little-endian]; NO_TONE takes an optional pin
 * byte and ignores it.
 *
 * The oracle is a model of the one tone generator, written as literals: tone
 * plays only on pin 11 (OC2A) and every other pin byte is unsupported, before
 * any range check; a frequency of zero stops it; a frequency below 490 Hz is
 * out of the AVR prescaler's reach, which the mock models, and is refused
 * leaving what was playing alone; a second start retunes the generator.
 *
 * After every request the frequency is compared with the model and every
 * pin's direction, level and pull-up with what it was, for a refusal.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "pwm.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "board_profile.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

#define TONE_PIN 11u
#define MIN_HZ   490u
#define SEED     0x163C70E5u
#define RANDOM_CASES 100000u

static const uint16_t PORTS[3] = { TFW_ADDR_PINB, TFW_ADDR_PINC, TFW_ADDR_PIND };

static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint32_t     rng;
static uint16_t     m_hz;   /* the model: what is playing, 0 for silence */

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

/* Every frequency on the tone pin, each started over the one before. */
static void test_every_frequency_on_the_tone_pin(void)
{
    char msg[48];

    for (uint32_t hz = 0u; hz <= 0xFFFFu; hz++) {
        snprintf(msg, sizeof msg, "hz %lu", (unsigned long)hz);
        start(TONE_PIN, (uint16_t)hz, msg);
    }
}

/* Every pin byte at frequencies on each side of the bound, with a tone
 * already playing: no other pin starts, retunes or silences it. */
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
static void test_every_other_length_is_refused(void)
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
static void test_random_requests_with_a_fixed_seed(void)
{
    char msg[48];

    rng = SEED;
    for (uint32_t i = 0u; i < RANDOM_CASES; i++) {
        uint32_t x   = rnd();
        uint8_t  pin = (x & 0x7u) ? TONE_PIN : (uint8_t)(x >> 24);

        snprintf(msg, sizeof msg, "seed 0x%08lx case %lu",
                 (unsigned long)SEED, (unsigned long)i);
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
    RUN_TEST(test_every_frequency_on_the_tone_pin);
    RUN_TEST(test_every_pin_byte_while_a_tone_plays);
    RUN_TEST(test_start_restart_stop_on_a_busy_pin);
    RUN_TEST(test_no_tone_with_every_pin_byte);
    RUN_TEST(test_every_other_length_is_refused);
    RUN_TEST(test_random_requests_with_a_fixed_seed);
    return UNITY_END();
}
