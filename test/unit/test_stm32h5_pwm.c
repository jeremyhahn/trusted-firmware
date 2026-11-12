/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 PWM's register values (src/hal/cortex-m/stm32h5/h5_calc.h,
 * hal_pwm.c) against a model of one general-purpose timer: the prescaler for
 * about 1 kHz, the duty's compare value, each channel's CCMR and CCER bits,
 * and stop. The model applies the helpers in hal_pwm.c's order; a board has
 * not run them.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "../../src/caps/pwm.h"

/* TIM_TypeDef's registers this port writes (CMSIS stm32h573xx.h). */
typedef struct {
    uint32_t CR1;
    uint32_t CCMR1;
    uint32_t CCMR2;
    uint32_t CCER;
    uint32_t PSC;
    uint32_t ARR;
    uint32_t CCR[4];
    uint32_t EGR;
} tim_model_t;

#define CR1_CEN  (0x1u << 0)   /* TIM_CR1_CEN_Pos 0 */
#define CR1_ARPE (0x1u << 7)   /* TIM_CR1_ARPE_Pos 7 */
#define EGR_UG   (0x1u << 0)   /* TIM_EGR_UG_Pos 0 */

static tim_model_t tim;

/* The suite's register file, TFW_CM_REG's target (test/model/cortex-m/
 * cm_regs.h, -DTFW_CM_MODEL_REGFILE): a word for each address the SoC files
 * touch, zero until written, as the registers this port uses reset. */
#define REGFILE_WORDS 32u
static struct {
    uintptr_t addr;
    uint32_t  val;
} regfile[REGFILE_WORDS];
static unsigned regfile_used;

volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    unsigned i;

    for (i = 0u; i < regfile_used; i++) {
        if (regfile[i].addr == addr) {
            return &regfile[i].val;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(regfile_used < REGFILE_WORDS, "register file full");
    regfile[regfile_used].addr = addr;
    return &regfile[regfile_used++].val;
}

/* The Arduino header's D6 and D5: PH10 and PH11, TIM5 channels 1 and 2
 * (boards/stm32h573i-dk/board.c). */
#define D6     4u
#define D5     5u
#define GPIOH  tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, 7u)

/* PWM_WRITE as the wire hands it to the command: pin, duty. */
static tfw_status_t wire_write(uint8_t pin, uint8_t duty)
{
    uint8_t      req[2] = { pin, duty };
    tfw_reader_t r;

    tfw_reader_init(&r, req, (uint8_t)sizeof req);
    return tfw_pwm_cmd_write(&r, NULL);
}

static uint32_t mode_of(uint8_t bit)
{
    return (TFW_H5_GPIO_MODER(GPIOH) >> (2u * bit)) & 0x3u;
}

static uint32_t af_of(uint8_t bit)
{
    return (TFW_H5_GPIO_AFRH(GPIOH) >> (4u * (bit & 7u))) & 0xFu;
}

/* hal_pwm.c's sequence: the timer's base once, then the channel. */
static void model_write(uint8_t channel, uint8_t duty, uint32_t timer_hz)
{
    if ((tim.CR1 & CR1_CEN) == 0u) {
        tim.PSC = tfw_h5_pwm_psc(timer_hz);
        tim.ARR = TFW_H5_PWM_ARR;
        tim.CR1 |= CR1_ARPE;
        tim.EGR = EGR_UG;
        tim.CR1 |= CR1_CEN;
    }
    if (channel <= 2u) {
        tim.CCMR1 = tfw_h5_pwm_ccmr(tim.CCMR1, channel);
    } else {
        tim.CCMR2 = tfw_h5_pwm_ccmr(tim.CCMR2, channel);
    }
    tim.CCR[channel - 1u] = duty;
    tim.CCER = tfw_h5_pwm_ccer(tim.CCER, channel, 1u);
}

static void model_stop(uint8_t channel)
{
    tim.CCER = tfw_h5_pwm_ccer(tim.CCER, channel, 0u);
}

/* PWM mode 1: the output is high while the counter, 0 to ARR, is below CCR. */
static uint32_t high_counts(uint8_t channel)
{
    uint32_t n = 0u;
    uint32_t cnt;

    for (cnt = 0u; cnt <= tim.ARR; cnt++) {
        if (cnt < tim.CCR[channel - 1u]) {
            n++;
        }
    }
    return n;
}

void setUp(void)
{
    memset(&tim, 0, sizeof(tim));
    memset(regfile, 0, sizeof(regfile));
    regfile_used = 0u;
}

/* hal_pwm.c keeps which channels are live; stop them all so each test
 * starts from reset. */
void tearDown(void)
{
    uint8_t ch;

    for (ch = 1u; ch <= 4u; ch++) {
        tfw_pwmref_t ref = { .unit = TFW_H5_PWM_UNIT, .channel = ch, .port = 7u, .bit = 10u };

        (void)tfw_hal_pwm_stop(&ref);
    }
}

/* The command makes the pin a GPIO output before each write (src/caps/
 * pwm.c); a second write must hand it back to the timer, or PWM_WRITE
 * answers OK with the pin a plain output. */
void test_each_wire_write_gives_the_pin_back_to_the_timer(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 100u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(10u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_AF, af_of(10u));
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 200u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(10u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_AF, af_of(10u));
    TEST_ASSERT_EQUAL_UINT32(200u, TFW_H5_TIM_CCR(1u));
}

void test_pwm_psc_1khz(void)
{
    uint16_t psc = tfw_h5_pwm_psc(64000000uL);
    uint32_t hz  = 64000000uL / ((uint32_t)psc + 1u) / TFW_H5_PWM_STEPS;

    TEST_ASSERT_EQUAL_UINT16(250u, psc);
    TEST_ASSERT_UINT32_WITHIN(5u, 1000u, hz);
}

void test_pwm_psc_holds_its_range(void)
{
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_pwm_psc(0u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_pwm_psc(255000u));
    /* The widest 32-bit clock: 16843 rounded, no overflow. */
    TEST_ASSERT_EQUAL_UINT16(16842u, tfw_h5_pwm_psc(0xFFFFFFFFu));
}

void test_duty_0_is_never_high(void)
{
    model_write(1u, 0u, 64000000uL);
    TEST_ASSERT_EQUAL_UINT32(0u, high_counts(1u));
}

void test_duty_128_is_half(void)
{
    model_write(1u, 128u, 64000000uL);
    TEST_ASSERT_EQUAL_UINT32(254u, tim.ARR);
    TEST_ASSERT_EQUAL_UINT32(128u, high_counts(1u));
}

void test_duty_255_is_always_high(void)
{
    model_write(1u, 255u, 64000000uL);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_STEPS, high_counts(1u));
}

void test_the_base_is_set_once_with_preload_and_update(void)
{
    model_write(2u, 10u, 64000000uL);
    TEST_ASSERT_EQUAL_UINT32(CR1_CEN | CR1_ARPE, tim.CR1);
    TEST_ASSERT_EQUAL_UINT32(EGR_UG, tim.EGR);
    tim.EGR = 0u;
    model_write(2u, 20u, 64000000uL);
    TEST_ASSERT_EQUAL_UINT32(0u, tim.EGR);
}

void test_each_channels_ccmr_bits(void)
{
    /* OCxM 0110 and OCxPE: 0x68 in the channel's byte, CCxS 00. */
    TEST_ASSERT_EQUAL_HEX32(0x00000068u, tfw_h5_pwm_ccmr(0u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x00006800u, tfw_h5_pwm_ccmr(0u, 2u));
    TEST_ASSERT_EQUAL_HEX32(0x00000068u, tfw_h5_pwm_ccmr(0u, 3u));
    TEST_ASSERT_EQUAL_HEX32(0x00006800u, tfw_h5_pwm_ccmr(0u, 4u));
    /* The other channel's half is kept; this one's OCxM_3 and CCxS cleared. */
    TEST_ASSERT_EQUAL_HEX32(0x0100FF68u, tfw_h5_pwm_ccmr(0x0101FFFFu, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x000168FFu, tfw_h5_pwm_ccmr(0x0101FFFFu, 2u));
}

void test_channels_three_and_four_use_ccmr2(void)
{
    model_write(3u, 1u, 64000000uL);
    model_write(4u, 1u, 64000000uL);
    TEST_ASSERT_EQUAL_HEX32(0u, tim.CCMR1);
    TEST_ASSERT_EQUAL_HEX32(0x00006868u, tim.CCMR2);
}

void test_each_channels_ccer_bit(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0001u, tfw_h5_pwm_ccer(0u, 1u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x0010u, tfw_h5_pwm_ccer(0u, 2u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x0100u, tfw_h5_pwm_ccer(0u, 3u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x1000u, tfw_h5_pwm_ccer(0u, 4u, 1u));
    /* Active high: CCxP cleared. */
    TEST_ASSERT_EQUAL_HEX32(0x0001u, tfw_h5_pwm_ccer(0x0002u, 1u, 1u));
}

void test_stop_clears_only_its_channel(void)
{
    model_write(1u, 100u, 64000000uL);
    model_write(2u, 100u, 64000000uL);
    model_stop(1u);
    TEST_ASSERT_EQUAL_HEX32(0x0010u, tim.CCER);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_psc_1khz);
    RUN_TEST(test_pwm_psc_holds_its_range);
    RUN_TEST(test_duty_0_is_never_high);
    RUN_TEST(test_duty_128_is_half);
    RUN_TEST(test_duty_255_is_always_high);
    RUN_TEST(test_the_base_is_set_once_with_preload_and_update);
    RUN_TEST(test_each_channels_ccmr_bits);
    RUN_TEST(test_channels_three_and_four_use_ccmr2);
    RUN_TEST(test_each_channels_ccer_bit);
    RUN_TEST(test_stop_clears_only_its_channel);
    RUN_TEST(test_each_wire_write_gives_the_pin_back_to_the_timer);
    return UNITY_END();
}
