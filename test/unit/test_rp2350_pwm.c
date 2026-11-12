/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RP2350 PWM: the divider helper in soc_calc.h and hal_pwm.c driven against
 * the flat register model (test/model/cortex-m/rp2350/), which stands in for
 * a PWM slice's CSR, DIV, CTR, CC and TOP and for the GPIO and SIO registers
 * the driver touches.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "rp2350_model.h"
#include "../../src/hal/cortex-m/rp2350/soc_regs.h"
#include "../../src/hal/cortex-m/rp2350/soc_calc.h"

/* GPIO n sits on slice (n >> 1) & 7, channel A when even, B when odd. */
static const tfw_pwmref_t gp2 = { 1u, 0u, 0u, 2u };
static const tfw_pwmref_t gp3 = { 1u, 1u, 0u, 3u };

void setUp(void)
{
    tfw_rp_model_reset();
    /* Every block reports out of reset, so the driver's wait ends at once. */
    tfw_rp_model_poke(TFW_RP_RESETS_BASE + 0x8u, 0xFFFFFFFFu);
}

void tearDown(void) {}

static uint32_t reg(uintptr_t addr) { return tfw_rp_model_peek(addr); }

/* 1 kHz with TOP 255 wants clk / 256000 in sixteenths: (clk + 8000) / 16000.
 * 12 MHz: 750 = INT 46, FRAC 14, so 12e6 / 46.875 / 256 = 1000 Hz. */
static void test_pwm_div_1khz(void)
{
    TEST_ASSERT_EQUAL_HEX16((46u << 4) | 14u, tfw_rp_pwm_div(12000000u));
}

/* 48 MHz: 3000 = INT 187, FRAC 8. 4.096 MHz: exactly INT 1. */
static void test_pwm_div_range(void)
{
    TEST_ASSERT_EQUAL_HEX16((187u << 4) | 8u, tfw_rp_pwm_div(48000000u));
    TEST_ASSERT_EQUAL_HEX16(1u << 4, tfw_rp_pwm_div(256000u));
}

/* Below INT 1 or above INT 255 no divider gives 1 kHz: 0 refuses. */
static void test_pwm_div_out_of_range(void)
{
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_rp_pwm_div(200000u));
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_rp_pwm_div(150000000u));
}

static void test_pwm_first_write_configures_slice(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 128u));
    TEST_ASSERT_EQUAL_HEX32((46u << 4) | 14u, reg(TFW_RP_PWM_DIV(1u)));
    TEST_ASSERT_EQUAL_HEX32(255u, reg(TFW_RP_PWM_TOP(1u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_PWM_CSR_EN, reg(TFW_RP_PWM_CSR(1u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_PWM, reg(0x40028000u + 0x004u + 8u * 2u));
    tfw_hal_pwm_stop(&gp2);
}

/* 0 is CC 0, never high; 255 is CC TOP + 1, never low; 128 is half. */
static void test_pwm_duty_compare_values(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 0u));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PWM_CC(1u)));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 128u));
    TEST_ASSERT_EQUAL_HEX32(128u, reg(TFW_RP_PWM_CC(1u)));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 255u));
    TEST_ASSERT_EQUAL_HEX32(256u, reg(TFW_RP_PWM_CC(1u)));
    tfw_hal_pwm_stop(&gp2);
}

/* A is CC[15:0], B is CC[31:16]; writing one leaves the other alone. */
static void test_pwm_channel_a_and_b(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 10u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp3, 200u));
    TEST_ASSERT_EQUAL_HEX32((200u << 16) | 10u, reg(TFW_RP_PWM_CC(1u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_PWM, reg(0x40028000u + 0x004u + 8u * 3u));
    tfw_hal_pwm_stop(&gp2);
}

/* Stop clears EN and hands the pin back to SIO, driven low. */
static void test_pwm_stop(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 128u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&gp2));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_PWM_CSR_EN, reg(TFW_RP_PWM_CSR(1u) + 0x3000u));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_SIO, reg(0x40028000u + 0x004u + 8u * 2u));
    TEST_ASSERT_EQUAL_HEX32(1u << 2, reg(TFW_RP_SIO_BASE + 0x020u));
    TEST_ASSERT_EQUAL_HEX32(1u << 2, reg(TFW_RP_SIO_BASE + 0x038u));
}

/* Slice 12 does not exist; a slice or channel that is not the GPIO's is
 * refused too, and nothing is written. */
static void test_pwm_bad_slice_refused(void)
{
    const tfw_pwmref_t no_slice  = { 12u, 0u, 0u, 2u };
    const tfw_pwmref_t wrong     = { 2u, 0u, 0u, 2u };
    const tfw_pwmref_t wrong_ch  = { 1u, 1u, 0u, 2u };
    const tfw_pwmref_t no_gpio   = { 1u, 0u, 0u, 30u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&no_slice, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&wrong, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&wrong_ch, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_write(&no_gpio, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_pwm_stop(&no_slice));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PWM_CSR(12u)));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PWM_CC(2u)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_div_1khz);
    RUN_TEST(test_pwm_div_range);
    RUN_TEST(test_pwm_div_out_of_range);
    RUN_TEST(test_pwm_first_write_configures_slice);
    RUN_TEST(test_pwm_duty_compare_values);
    RUN_TEST(test_pwm_channel_a_and_b);
    RUN_TEST(test_pwm_stop);
    RUN_TEST(test_pwm_bad_slice_refused);
    return UNITY_END();
}
