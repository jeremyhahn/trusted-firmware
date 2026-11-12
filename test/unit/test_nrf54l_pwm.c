/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l PWM (src/hal/cortex-m/nrf54l/hal_pwm.c) against the register
 * model (test/model/nrf54l_model.c), since nothing on the host plays a
 * PWM20 sequence. The register offsets are soc_regs.h's, each cited to the
 * nrfx MDK; what is checked here is what the HAL writes to them and to the
 * RAM sequence the peripheral reads.
 *
 * The HAL configures PWM20 once, on first use, and keeps that state across
 * these tests as it does across commands: the model is not reset between
 * them, and tearDown takes every channel off its pin.
 */

#include "unity.h"
#include "tfw/board.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "../../src/caps/pwm.h"

extern volatile uint16_t tfw_nrf54l_pwm_seq[4];

/* P1.10 and P1.14, LED1 and LED3 on the DK: the board's two rows. */
static const tfw_pwmref_t ch0 = { 0u, 0u, 1u, 10u };
static const tfw_pwmref_t ch1 = { 0u, 1u, 1u, 14u };

#define P1 TFW_NRF_P1_BASE

void setUp(void)
{
    TFW_NRF_PWM_STOP       = 0u;
    TFW_NRF_PWM_SEQ0_START = 0u;
    TFW_NRF_GPIO_OUTSET(P1) = 0u;
    TFW_NRF_GPIO_OUTCLR(P1) = 0u;
}

void tearDown(void)
{
    (void)tfw_hal_pwm_stop(&ch0);
    (void)tfw_hal_pwm_stop(&ch1);
}

static void test_first_write_configures_pwm20_up_counting_at_1khz(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 128u));
    TEST_ASSERT_EQUAL_HEX32(1u, TFW_NRF_PWM_ENABLE);       /* Enabled */
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_PWM_MODE);         /* UPDOWN Up */
    TEST_ASSERT_EQUAL_HEX32(4u, TFW_NRF_PWM_PRESCALER);    /* DIV_16, 1 MHz */
    TEST_ASSERT_EQUAL_UINT32(1000u, TFW_NRF_PWM_COUNTERTOP);
    TEST_ASSERT_EQUAL_HEX32(2u, TFW_NRF_PWM_DECODER);      /* Individual, RefreshCount */
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_LOOP);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_SEQ0_REFRESH);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)(uintptr_t)tfw_nrf54l_pwm_seq, TFW_NRF_PWM_SEQ0_PTR);
    TEST_ASSERT_EQUAL_UINT32(8u, TFW_NRF_PWM_SEQ0_MAXCNT);  /* four half-words, in bytes */
}

static void test_a_duty_is_scaled_to_countertop_and_played(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 128u));
    /* 128 / 255 of 1000, rounded, with bit 15 for a pin high first. */
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 502u, tfw_nrf54l_pwm_seq[0]);
    TEST_ASSERT_EQUAL_HEX32(0x2Au, TFW_NRF_PWM_PSEL_OUT(0));  /* PORT 1, PIN 10, CONNECT */
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_OUTPUT, TFW_NRF_GPIO_PIN_CNF(P1, 10u));
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_PWM_SEQ0_START);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 1u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 4u, tfw_nrf54l_pwm_seq[0]);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 254u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 996u, tfw_nrf54l_pwm_seq[0]);
}

static void test_duty_0_drives_the_pin_low_without_the_peripheral(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 100u));
    TFW_NRF_PWM_SEQ0_START = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 0u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, TFW_NRF_PWM_PSEL_OUT(0));  /* disconnected */
    TEST_ASSERT_EQUAL_HEX32(1uL << 10, TFW_NRF_GPIO_OUTCLR(P1));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_NRF_GPIO_OUTSET(P1));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_SEQ0_START);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_PWM_STOP);  /* it was the only channel */
}

static void test_duty_255_drives_the_pin_high_without_the_peripheral(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 255u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, TFW_NRF_PWM_PSEL_OUT(0));
    TEST_ASSERT_EQUAL_HEX32(1uL << 10, TFW_NRF_GPIO_OUTSET(P1));
    TEST_ASSERT_EQUAL_HEX32(TFW_NRF_PIN_CNF_OUTPUT, TFW_NRF_GPIO_PIN_CNF(P1, 10u));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_SEQ0_START);
}

/* The wire path: PWM_WRITE goes through src/caps/pwm.c, which routes duty
 * zero to tfw_hal_pwm_stop(). After duty 255 set the pin high with OUTSET,
 * duty zero must drive it low again with OUTCLR. */
static tfw_status_t wire_pwm_write(uint8_t pin, uint8_t duty)
{
    uint8_t      buf[2] = { pin, duty };
    tfw_reader_t r;

    tfw_reader_init(&r, buf, (uint8_t)sizeof buf);
    return tfw_pwm_cmd_write(&r, NULL);
}

static void test_wire_duty_0_after_255_drives_the_pin_low(void)
{
    uint32_t out;

    /* DK pin 1 is LED1, P1.10, PWM20 channel 0. */
    TEST_ASSERT_EQUAL(TFW_OK, wire_pwm_write(1u, 255u));
    TEST_ASSERT_EQUAL_HEX32(1uL << 10, TFW_NRF_GPIO_OUTSET(P1));
    out = TFW_NRF_GPIO_OUTSET(P1);
    TFW_NRF_GPIO_OUTSET(P1) = 0u;
    TFW_NRF_GPIO_OUTCLR(P1) = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, wire_pwm_write(1u, 0u));
    TEST_ASSERT_EQUAL_HEX32(1uL << 10, TFW_NRF_GPIO_OUTCLR(P1));
    out = (out | TFW_NRF_GPIO_OUTSET(P1)) & ~TFW_NRF_GPIO_OUTCLR(P1);
    TEST_ASSERT_EQUAL_HEX32(0u, out & (1uL << 10));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, TFW_NRF_PWM_PSEL_OUT(0));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_SEQ0_START);
}

static void test_a_second_channel_plays_beside_the_first(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch0, 51u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch1, 204u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 200u, tfw_nrf54l_pwm_seq[0]);
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 800u, tfw_nrf54l_pwm_seq[1]);
    TEST_ASSERT_EQUAL_HEX32(0x2Au, TFW_NRF_PWM_PSEL_OUT(0));
    TEST_ASSERT_EQUAL_HEX32(0x2Eu, TFW_NRF_PWM_PSEL_OUT(1));  /* PORT 1, PIN 14 */

    /* Stopping one leaves the other playing. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&ch0));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_STOP);
    TEST_ASSERT_EQUAL_HEX32(0x2Eu, TFW_NRF_PWM_PSEL_OUT(1));
    TEST_ASSERT_EQUAL_HEX16(0x8000u | 800u, tfw_nrf54l_pwm_seq[1]);
}

static void test_stop_disconnects_and_stops_the_last_channel(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&ch1, 77u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&ch1));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, TFW_NRF_PWM_PSEL_OUT(1));
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_nrf54l_pwm_seq[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, TFW_NRF_PWM_STOP);

    /* A second stop has nothing left to stop. */
    TFW_NRF_PWM_STOP = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&ch1));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_STOP);
}

static void test_a_ref_the_unit_has_no_channel_for_is_refused(void)
{
    static const tfw_pwmref_t no_pin   = { 0u, 0u, 1u, 17u };  /* P1 ends at P1.16 */
    static const tfw_pwmref_t no_chan  = { 0u, 4u, 1u, 10u };
    static const tfw_pwmref_t no_unit  = { 1u, 0u, 1u, 10u };

    TFW_NRF_PWM_PSEL_OUT(0) = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&no_pin, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_stop(&no_pin));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_write(&no_chan, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_write(&no_unit, 128u));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_PSEL_OUT(0));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_NRF_PWM_SEQ0_START);
}

static void test_the_board_resolves_led1_and_led3_to_pwm20_channels_0_and_1(void)
{
    /* The nRF54L15-DK's tfw_board_pwms rows, through tfw_board_pwm():
     * PWM20 (unit 0) reaches port 1 only, LED1 P1.10 on channel 0 and
     * LED3 P1.14 on channel 1. */
    static const uint8_t pin[2] = { 1u, 3u };
    static const uint8_t bit[2] = { 10u, 14u };

    for (uint8_t i = 0u; i < 2u; i++) {
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(pin[i], &p));
        TEST_ASSERT_EQUAL_UINT8(0u, p.unit);
        TEST_ASSERT_EQUAL_UINT8(i, p.channel);
        TEST_ASSERT_EQUAL_UINT8(1u, p.port);
        TEST_ASSERT_EQUAL_UINT8(bit[i], p.bit);
    }
}

int main(void)
{
    tfw_nrf54l_model_reset();
    UNITY_BEGIN();
    RUN_TEST(test_first_write_configures_pwm20_up_counting_at_1khz);
    RUN_TEST(test_a_duty_is_scaled_to_countertop_and_played);
    RUN_TEST(test_duty_0_drives_the_pin_low_without_the_peripheral);
    RUN_TEST(test_duty_255_drives_the_pin_high_without_the_peripheral);
    RUN_TEST(test_wire_duty_0_after_255_drives_the_pin_low);
    RUN_TEST(test_a_second_channel_plays_beside_the_first);
    RUN_TEST(test_stop_disconnects_and_stops_the_last_channel);
    RUN_TEST(test_a_ref_the_unit_has_no_channel_for_is_refused);
    RUN_TEST(test_the_board_resolves_led1_and_led3_to_pwm20_channels_0_and_1);
    return UNITY_END();
}
