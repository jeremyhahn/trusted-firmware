/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The signal capabilities -- analog, pwm, tone, pulse -- under mutation (issue
 * #163), through the dispatcher and the mock HAL as test_analog.c, test_pwm.c
 * and test_pulse.c drive them.
 *
 * Every pin 0..255 and every argument at its edges and past them answers the
 * status those tests define: BAD_PIN past the board, UNSUPPORTED for a pin
 * without the unit, RESERVED_PIN for a reserved or in-use one, RANGE for an
 * argument out of bounds, BAD_ARGC for a payload of any other length. Every
 * refusal leaves the mock as it was -- every board pin's level, direction and
 * pull-up, every PWM unit's duty and connection, the tone, the ADC reference,
 * EEPROM and the UART -- and writes no reply byte.
 *
 * The expected pin status is read from the board's own table (tfw_board_*),
 * so the sweep holds on every board the host build is pointed at. Each
 * payload is a heap buffer of exactly its length, so under `make test-asan` a
 * read past it is a sanitizer report. No randomness.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "analog.h"
#include "pwm.h"
#include "pulse.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define CAPTURE_MAX ((uint8_t)((TFW_MAX_PAYLOAD - 1u) / 2u))   /* as pulse.c */
#define TONE_HZ     1000u
#define MAX_PINS    256u

typedef struct {
    uint8_t  level[MAX_PINS], output[MAX_PINS], pullup[MAX_PINS];
    uint8_t  duty[MAX_PINS], connected[MAX_PINS];
    uint16_t tone_hz;
    uint8_t  adc_ref;
    size_t   ee_written;
    size_t   tx_pending;
} mock_state_t;

static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_writer_t w;

/* A state that differs from reset: a tone sounding, a non-default reference,
 * and a PWM running, so a refusal that clears any of them is seen. */
void setUp(void)
{
    tfw_mock_reset();
    tfw_hal_tone_start(0, TONE_HZ);
    tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL);
}

void tearDown(void) {}

static void snapshot(mock_state_t *s)
{
    tfw_pinref_t pr;
    tfw_pwmref_t wr;
    uint16_t     lo, hi;
    unsigned     pin;

    memset(s, 0, sizeof *s);
    for (pin = 0u; pin < tfw_board_pin_count(); pin++) {
        if (tfw_board_pin((tfw_pin_t)pin, &pr) == TFW_OK) {
            s->level[pin]  = tfw_mock_pin_level(pr.port, pr.bit);
            s->output[pin] = tfw_mock_pin_is_output(pr.port, pr.bit);
            s->pullup[pin] = tfw_mock_pin_pullup(pr.port, pr.bit);
        }
        if (tfw_board_pwm((tfw_pin_t)pin, &wr) == TFW_OK) {
            s->duty[pin]      = tfw_mock_pwm_duty(wr.duty);
            s->connected[pin] = tfw_mock_pwm_connected(wr.ctl, wr.bit);
        }
    }
    s->tone_hz    = tfw_mock_tone_hz();
    s->adc_ref    = tfw_mock_adc_ref();
    s->ee_written = tfw_mock_eeprom_written(&lo, &hi);
    s->tx_pending = tfw_mock_tx_pending();
}

/* Dispatch cmd over a heap copy of exactly n bytes. */
static tfw_status_t run(uint8_t cmd, const uint8_t *p, unsigned n)
{
    uint8_t     *h = (uint8_t *)malloc(n ? n : 1u);
    tfw_reader_t r;
    tfw_status_t st;

    TEST_ASSERT_NOT_NULL(h);
    if (n) {
        memcpy(h, p, n);
    }
    tfw_reader_init(&r, h, (uint8_t)n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    st = tfw_dispatch(cmd, &r, &w);
    free(h);
    return st;
}

/* The request answers want; a refusal writes nothing and changes nothing. */
static void expect(uint8_t cmd, const uint8_t *p, unsigned n, tfw_status_t want, const char *what,
                   unsigned a, unsigned b)
{
    mock_state_t before, after;
    tfw_status_t st;

    snapshot(&before);
    st = run(cmd, p, n);
    snapshot(&after);
    if (st != want) {
        printf("cmd 0x%02x %s %u/%u: status %d, want %d\n", cmd, what, a, b, (int)st, (int)want);
        TEST_FAIL_MESSAGE("wrong status");
    }
    if (want != TFW_OK && (w.pos != 0u || memcmp(&before, &after, sizeof before) != 0)) {
        printf("cmd 0x%02x %s %u/%u: refused with %u reply bytes or a changed mock\n", cmd, what, a,
               b, (unsigned)w.pos);
        TEST_FAIL_MESSAGE("a refusal changed the mock or wrote a reply");
    }
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The first digital pin that exists and has a port, for the argument sweeps. */
static uint8_t a_digital_pin(void)
{
    tfw_pinref_t pr;
    unsigned     pin;

    for (pin = 0u; pin < tfw_board_pin_count(); pin++) {
        if (tfw_board_pin((tfw_pin_t)pin, &pr) == TFW_OK) {
            return (uint8_t)pin;
        }
    }
    TEST_FAIL_MESSAGE("the board has no digital pin");
    return 0u;
}

static void test_signal_mutate_analog_read_every_pin(void)
{
    unsigned     pin;
    uint8_t      ch;
    tfw_status_t want;

    for (pin = 0u; pin <= 0xFFu; pin++) {
        const uint8_t p[1] = { (uint8_t)pin };

        want = tfw_board_adc_channel((tfw_pin_t)pin, &ch);
        if (want == TFW_OK && ch > 7u) {
            want = TFW_ERR_BAD_PIN;           /* the mock's converter has eight */
        }
        expect(TFW_CMD_ANALOG_READ, p, 1u, want, "pin", pin, 0u);
        if (want == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8(2u, w.pos);
            /* a converter that times out answers TIMEOUT and writes nothing */
            tfw_mock_adc_read_fails(1u);
            expect(TFW_CMD_ANALOG_READ, p, 1u, TFW_ERR_TIMEOUT, "timeout pin", pin, 0u);
            tfw_mock_adc_read_fails(0u);
        }
    }
}

static void test_signal_mutate_adc_ref_every_value(void)
{
    unsigned ref;

    for (ref = 0u; ref <= 0xFFu; ref++) {
        const uint8_t p[1] = { (uint8_t)ref };

        if (ref <= (unsigned)TFW_ADC_REF_INTERNAL) {
            expect(TFW_CMD_ADC_REF, p, 1u, TFW_OK, "ref", ref, 0u);
            TEST_ASSERT_EQUAL_UINT8(ref, tfw_mock_adc_ref());
            tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL);
        } else {
            expect(TFW_CMD_ADC_REF, p, 1u, TFW_ERR_RANGE, "ref", ref, 0u);
        }
    }
}

static void test_signal_mutate_pwm_write_every_pin_and_duty(void)
{
    static const uint8_t duties[] = { 0u, 1u, 2u, 127u, 128u, 254u, 255u };
    tfw_pwmref_t wr;
    tfw_pinref_t pr;
    tfw_status_t want;
    unsigned     pin, d, duty;

    for (pin = 0u; pin <= 0xFFu; pin++) {
        for (d = 0u; d < sizeof duties; d++) {
            const uint8_t p[2] = { (uint8_t)pin, duties[d] };

            duty = duties[d];

            if (tfw_board_pin_flags((tfw_pin_t)pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
                want = TFW_ERR_RESERVED_PIN;
            } else if ((want = tfw_board_pwm((tfw_pin_t)pin, &wr)) == TFW_OK) {
                want = tfw_board_pin((tfw_pin_t)pin, &pr);
            }
            expect(TFW_CMD_PWM_WRITE, p, 2u, want, "pin/duty", pin, duty);
            if (want == TFW_OK) {
                TEST_ASSERT_EQUAL_UINT8(duty != 0u, tfw_mock_pwm_connected(wr.ctl, wr.bit));
                if (duty != 0u) {
                    TEST_ASSERT_EQUAL_UINT8(duty, tfw_mock_pwm_duty(wr.duty));
                }
                TEST_ASSERT_TRUE(tfw_mock_pin_is_output(pr.port, pr.bit));
            }
        }
    }
}

static void test_signal_mutate_tone_every_pin(void)
{
    unsigned     pin;
    tfw_status_t want;
    tfw_pinref_t pr;

    for (pin = 0u; pin <= 0xFFu; pin++) {
        const uint8_t p[3] = { (uint8_t)pin, (uint8_t)(2000u & 0xFFu), (uint8_t)(2000u >> 8) };

        if (pin != TFW_TONE_PIN) {
            want = TFW_ERR_UNSUPPORTED;
        } else if (tfw_board_pin_flags((tfw_pin_t)pin) & (TFW_PIN_RESERVED | TFW_PIN_IN_USE)) {
            want = TFW_ERR_RESERVED_PIN;
        } else {
            want = tfw_board_pin((tfw_pin_t)pin, &pr);
        }
        expect(TFW_CMD_TONE, p, 3u, want, "pin", pin, 0u);
        if (want == TFW_OK) {
            TEST_ASSERT_EQUAL_UINT16(2000u, tfw_mock_tone_hz());
            tfw_hal_tone_start(0, TONE_HZ);
        }
    }
}

/* The mock models the AVR prescaler's floor of 490 Hz; 0 stops the tone. */
static void test_signal_mutate_tone_every_frequency_edge(void)
{
    static const uint16_t hz[] = { 0u, 1u, 2u, 255u, 256u, 488u, 489u, 490u, 491u, 0x7FFFu,
                                   0x8000u, 0xFFFEu, 0xFFFFu };
    unsigned i;

    for (i = 0u; i < sizeof hz / sizeof hz[0]; i++) {
        const uint8_t p[3] = { (uint8_t)TFW_TONE_PIN, (uint8_t)hz[i], (uint8_t)(hz[i] >> 8) };

        expect(TFW_CMD_TONE, p, 3u, (hz[i] != 0u && hz[i] < 490u) ? TFW_ERR_RANGE : TFW_OK, "hz",
               hz[i], 0u);
        TEST_ASSERT_EQUAL_UINT16((hz[i] != 0u && hz[i] < 490u) ? TONE_HZ : hz[i],
                                 tfw_mock_tone_hz());
        tfw_hal_tone_start(0, TONE_HZ);
    }
}

static void test_signal_mutate_pulse_in_every_pin_level_and_timeout(void)
{
    static const uint32_t timeouts[] = { 0u, 1u, 2u, TFW_PULSE_MAX_US - 1u, TFW_PULSE_MAX_US,
                                         TFW_PULSE_MAX_US + 1u, 0x80000000u, 0xFFFFFFFFu };
    const uint8_t good = a_digital_pin();
    tfw_pinref_t  pr;
    uint8_t       p[6];
    unsigned      pin, level, i;

    tfw_mock_set_pulse_in(1u);
    for (pin = 0u; pin <= 0xFFu; pin++) {
        p[0] = (uint8_t)pin;
        p[1] = 1u;
        le32(&p[2], 1000u);
        expect(TFW_CMD_PULSE_IN, p, 6u, tfw_board_pin((tfw_pin_t)pin, &pr), "pin", pin, 0u);
    }
    for (level = 0u; level <= 0xFFu; level++) {
        p[0] = good;
        p[1] = (uint8_t)level;
        le32(&p[2], 1000u);
        expect(TFW_CMD_PULSE_IN, p, 6u, level <= 1u ? TFW_OK : TFW_ERR_RANGE, "level", level, 0u);
    }
    for (i = 0u; i < sizeof timeouts / sizeof timeouts[0]; i++) {
        p[0] = good;
        p[1] = 0u;
        le32(&p[2], timeouts[i]);
        expect(TFW_CMD_PULSE_IN, p, 6u,
               (timeouts[i] == 0u || timeouts[i] > TFW_PULSE_MAX_US) ? TFW_ERR_RANGE : TFW_OK,
               "timeout", (unsigned)timeouts[i], 0u);
    }
}

static void test_signal_mutate_pulse_capture_every_pin_start_max_and_timeout(void)
{
    static const uint16_t edges[2] = { 26u, 70u };
    static const uint32_t timeouts[] = { 0u, 1u, TFW_PULSE_MAX_US, TFW_PULSE_MAX_US + 1u,
                                         0xFFFFFFFFu };
    const uint8_t good = a_digital_pin();
    tfw_pinref_t  pr;
    uint8_t       p[7];
    unsigned      pin, start, max, i;

    tfw_mock_set_capture(edges, 2u);
    for (pin = 0u; pin <= 0xFFu; pin++) {
        p[0] = (uint8_t)pin;
        p[1] = 0u;
        p[2] = 2u;
        le32(&p[3], 1000u);
        expect(TFW_CMD_PULSE_CAPTURE, p, 7u, tfw_board_pin((tfw_pin_t)pin, &pr), "pin", pin, 0u);
    }
    for (start = 0u; start <= 0xFFu; start++) {
        p[0] = good;
        p[1] = (uint8_t)start;
        p[2] = 2u;
        le32(&p[3], 1000u);
        expect(TFW_CMD_PULSE_CAPTURE, p, 7u, start <= 1u ? TFW_OK : TFW_ERR_RANGE, "start", start,
               0u);
    }
    for (max = 0u; max <= 0xFFu; max++) {
        p[0] = good;
        p[1] = 0u;
        p[2] = (uint8_t)max;
        le32(&p[3], 1000u);
        expect(TFW_CMD_PULSE_CAPTURE, p, 7u,
               (max == 0u || max > CAPTURE_MAX) ? TFW_ERR_RANGE : TFW_OK, "max", max, 0u);
    }
    for (i = 0u; i < sizeof timeouts / sizeof timeouts[0]; i++) {
        p[0] = good;
        p[1] = 0u;
        p[2] = 2u;
        le32(&p[3], timeouts[i]);
        expect(TFW_CMD_PULSE_CAPTURE, p, 7u,
               (timeouts[i] == 0u || timeouts[i] > TFW_PULSE_MAX_US) ? TFW_ERR_RANGE : TFW_OK,
               "timeout", (unsigned)timeouts[i], 0u);
    }
}

/* Every payload length other than the command's own is BAD_ARGC, at every
 * length a frame can carry, before the handler reads a byte. */
static void test_signal_mutate_every_wrong_length_is_bad_argc(void)
{
    static const struct { uint8_t cmd, min, max; } cmds[] = {
        { TFW_CMD_ANALOG_READ, 1u, 1u }, { TFW_CMD_ADC_REF, 1u, 1u },
        { TFW_CMD_PWM_WRITE, 2u, 2u },   { TFW_CMD_TONE, 3u, 3u },
        { TFW_CMD_NO_TONE, 0u, 1u },     { TFW_CMD_PULSE_IN, 6u, 6u },
        { TFW_CMD_PULSE_CAPTURE, 7u, 7u },
    };
    uint8_t  p[TFW_MAX_PAYLOAD];
    unsigned c, n;

    /* a payload that would be accepted at its own length: a good pin, 0xFF fill */
    memset(p, 0xFF, sizeof p);
    p[0] = TFW_TONE_PIN;
    for (c = 0u; c < sizeof cmds / sizeof cmds[0]; c++) {
        for (n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
            if (n >= cmds[c].min && n <= cmds[c].max) {
                continue;
            }
            expect(cmds[c].cmd, p, n, TFW_ERR_BAD_ARGC, "len", n, 0u);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_signal_mutate_analog_read_every_pin);
    RUN_TEST(test_signal_mutate_adc_ref_every_value);
    RUN_TEST(test_signal_mutate_pwm_write_every_pin_and_duty);
    RUN_TEST(test_signal_mutate_tone_every_pin);
    RUN_TEST(test_signal_mutate_tone_every_frequency_edge);
    RUN_TEST(test_signal_mutate_pulse_in_every_pin_level_and_timeout);
    RUN_TEST(test_signal_mutate_pulse_capture_every_pin_start_max_and_timeout);
    RUN_TEST(test_signal_mutate_every_wrong_length_is_bad_argc);
    return UNITY_END();
}
