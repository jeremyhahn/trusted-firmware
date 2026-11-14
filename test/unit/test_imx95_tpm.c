/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

/* The i.MX 95 M7's PWM on a TPM (src/hal/cortex-m/imx95/hal_pwm.c, tpm.h;
 * SPEC row `imx95`, #22), against a TPM register model of this file's own:
 * QEMU has no i.MX 95, and test/model/imx95_model.c is shared with the
 * port's other lanes. The SoC files reach the model as test_imx95_hal's do,
 * through test/model/cortex-m/cm_regs.h's TFW_CM_REG, which calls
 * tfw_imx95_model_reg: this file defines it, for the TPM alone. */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "imx95_model.h"
#include "board_profile.h"
#include "tpm.h"
#include "soc_regs.h"
#include "tfw/board.h"
#include "reader.h"
#include "pwm.h"

/* The model's TPM: any non-zero base will do, the HAL is handed it. */
#define MODEL_BASE  0x10000000u
#define MODEL_SPAN  0x88u        /* PARAM to CONF, words */
#define MODEL_CHANS 4u           /* PARAM.CHAN the model reports */

/* GPIO2, the bank the board's pins are on (soc_calc.c), for the command
 * through src/caps/pwm.c: only its direction register is modelled. */
#define MODEL_GPIO2 0x43810000u

static uint32_t model_regs[MODEL_SPAN / 4u];
static uint32_t model_pddr;      /* GPIO2's PDDR */
static uint32_t model_psor;      /* GPIO2's PSOR: DIGITAL_WRITE's high */
static uint32_t model_stray;     /* any address outside the TPM */
static unsigned model_touches;   /* every access, inside or out */
static unsigned model_strays;

volatile uint32_t *tfw_imx95_model_reg(uintptr_t addr)
{
    model_touches++;
    if (addr >= MODEL_BASE && addr < MODEL_BASE + MODEL_SPAN && (addr & 3u) == 0u) {
        return &model_regs[(addr - MODEL_BASE) / 4u];
    }
    if (addr == MODEL_GPIO2 + TFW_IMX95_GPIO_PDDR) {
        return &model_pddr;
    }
    if (addr == MODEL_GPIO2 + TFW_IMX95_GPIO_PSOR) {
        return &model_psor;
    }
    model_strays++;
    return &model_stray;
}

uint32_t tfw_imx95_model_irq_save(void)
{
    return 0u;
}

void tfw_imx95_model_irq_restore(uint32_t primask)
{
    (void)primask;
}

static uint32_t reg(uint32_t off)
{
    return model_regs[off / 4u];
}

static void set_reg(uint32_t off, uint32_t v)
{
    model_regs[off / 4u] = v;
}

static const tfw_pwmref_t ch0 = { 0u, 0u, 1u, 2u };
static const tfw_pwmref_t ch1 = { 0u, 1u, 1u, 3u };

void setUp(void)
{
    memset(model_regs, 0, sizeof model_regs);
    model_pddr    = 0u;
    model_psor    = 0u;
    model_stray   = 0u;
    model_touches = 0u;
    model_strays  = 0u;
    set_reg(TFW_IMX95_TPM_PARAM, MODEL_CHANS);
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, model_strays);
    /* STATUS and CONF are never the HAL's. */
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_IMX95_TPM_STATUS));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_IMX95_TPM_CONF));
}

static tfw_status_t write_at(const tfw_pwmref_t *p, uint8_t duty)
{
    return tfw_imx95_tpm_write(MODEL_BASE, (uint32_t)TFW_IMX95_TPM_CLK_HZ, p, duty);
}

/* 1 kHz from the 24 MHz root: 24000 counts, MOD 23999; half duty (128) is
 * 128 * 24000 / 255 counts high, edge-aligned, counting every clock. */
static void test_tpm_duty_half(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch0, 128u));
    TEST_ASSERT_EQUAL_HEX32(23999u, reg(TFW_IMX95_TPM_MOD));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_SC_CMOD_INC, reg(TFW_IMX95_TPM_SC));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_CnSC_MSB | TFW_IMX95_TPM_CnSC_ELS_NORM,
                            reg(TFW_IMX95_TPM_CnSC(0)));
    TEST_ASSERT_EQUAL_UINT32(12047u, reg(TFW_IMX95_TPM_CnV(0)));
}

static void check_period(uint32_t clk, uint32_t hz, uint32_t want_ps, uint32_t want_mod)
{
    uint32_t ps  = 99u;
    uint32_t mod = 99u;

    tfw_imx95_tpm_period(clk, hz, &ps, &mod);
    TEST_ASSERT_EQUAL_UINT32(want_ps, ps);
    TEST_ASSERT_EQUAL_UINT32(want_mod, mod);
}

static void test_tpm_period_for_several_frequencies(void)
{
    check_period(24000000u, 1000u, 0u, 23999u);
    check_period(24000000u, 367u, 0u, 65394u);     /* the largest that fits PS 0 */
    check_period(24000000u, 366u, 1u, 32785u);     /* 65573 counts: PS 1 */
    check_period(24000000u, 100u, 2u, 59999u);
    check_period(24000000u, 50u, 3u, 59999u);
    check_period(24000000u, 1u, 7u, 65535u);       /* too long even at PS 7: clamped */
    check_period(24000000u, 24000000u, 0u, 0u);
    check_period(1000u, 2000u, 0u, 0u);            /* clock slower than the PWM */
    check_period(24000000u, 0u, 0u, 0u);
}

static void test_tpm_cnv_for_several_duties(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_imx95_tpm_cnv(23999u, 0u));
    TEST_ASSERT_EQUAL_UINT32(6023u, tfw_imx95_tpm_cnv(23999u, 64u));
    TEST_ASSERT_EQUAL_UINT32(12047u, tfw_imx95_tpm_cnv(23999u, 128u));
    TEST_ASSERT_EQUAL_UINT32(24000u, tfw_imx95_tpm_cnv(23999u, 255u));
    TEST_ASSERT_EQUAL_UINT32(30117u, tfw_imx95_tpm_cnv(59999u, 128u));
    TEST_ASSERT_EQUAL_UINT32(65536u, tfw_imx95_tpm_cnv(65535u, 255u));
}

/* Duty 0 never sets the output (CnV 0: low); 255 puts CnV past MOD, so the
 * output is never cleared (high). */
static void test_tpm_duty_0_is_low_and_255_is_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch0, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, reg(TFW_IMX95_TPM_CnV(0)));
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch0, 255u));
    TEST_ASSERT_TRUE(reg(TFW_IMX95_TPM_CnV(0)) > reg(TFW_IMX95_TPM_MOD));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_CnSC_MSB | TFW_IMX95_TPM_CnSC_ELS_NORM,
                            reg(TFW_IMX95_TPM_CnSC(0)));
}

/* A second channel shares the running counter: SC and MOD are not written
 * again (a bit the HAL does not set, left in SC, survives). */
static void test_tpm_second_channel_leaves_the_counter_alone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch0, 128u));
    set_reg(TFW_IMX95_TPM_SC, reg(TFW_IMX95_TPM_SC) | 0x40u);
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch1, 64u));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_SC_CMOD_INC | 0x40u, reg(TFW_IMX95_TPM_SC));
    TEST_ASSERT_EQUAL_UINT32(6023u, reg(TFW_IMX95_TPM_CnV(1)));
    TEST_ASSERT_EQUAL_UINT32(12047u, reg(TFW_IMX95_TPM_CnV(0)));
}

/* Stop disables the channel and leaves the counter and the other channel. */
static void test_tpm_stop_disables_the_channel_only(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch0, 128u));
    TEST_ASSERT_EQUAL(TFW_OK, write_at(&ch1, 64u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_imx95_tpm_stop(MODEL_BASE, &ch0));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_IMX95_TPM_CnSC(0)));
    TEST_ASSERT_EQUAL_UINT32(0u, reg(TFW_IMX95_TPM_CnV(0)));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_SC_CMOD_INC, reg(TFW_IMX95_TPM_SC));
    TEST_ASSERT_EQUAL_UINT32(23999u, reg(TFW_IMX95_TPM_MOD));
    TEST_ASSERT_EQUAL_HEX32(TFW_IMX95_TPM_CnSC_MSB | TFW_IMX95_TPM_CnSC_ELS_NORM,
                            reg(TFW_IMX95_TPM_CnSC(1)));
    TEST_ASSERT_EQUAL_UINT32(6023u, reg(TFW_IMX95_TPM_CnV(1)));
}

/* A channel the TPM does not have (PARAM.CHAN), or another unit, is
 * TFW_ERR_RANGE, and no channel register is written. */
static void test_tpm_a_pin_with_no_channel_is_range(void)
{
    const tfw_pwmref_t past  = { 0u, MODEL_CHANS, 1u, 2u };
    const tfw_pwmref_t other = { 1u, 0u, 1u, 2u };
    uint32_t before[MODEL_SPAN / 4u];

    memcpy(before, model_regs, sizeof before);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, write_at(&past, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_tpm_stop(MODEL_BASE, &past));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, write_at(&other, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_imx95_tpm_stop(MODEL_BASE, &other));
    TEST_ASSERT_EQUAL_MEMORY(before, model_regs, sizeof before);
}

/* Base zero is unsupported: no register is read or written. The board's
 * TFW_IMX95_TPM_BASE is zero until the M7's instance is known, so the HAL's
 * own entry points answer the same. */
#if TFW_IMX95_TPM_BASE == 0u
static void test_tpm_base_zero_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                      tfw_imx95_tpm_write(0u, (uint32_t)TFW_IMX95_TPM_CLK_HZ, &ch0, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_imx95_tpm_stop(0u, &ch0));
    TEST_ASSERT_EQUAL(0u, TFW_IMX95_TPM_BASE);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_write(&ch0, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_stop(&ch0));
    TEST_ASSERT_EQUAL_UINT(0u, model_touches);
}
#endif

/* Codex P1 (board.c:35): with the TPM unknown, PWM_WRITE on pin 2 is refused
 * by the board table before src/caps/pwm.c makes the pin an output, so a
 * refused request never drives what is attached to an input. Pin 3, an
 * output already, keeps its direction too; duty 0 (stop) is refused alike. */
static void test_pwm_write_refused_leaves_the_pin_an_input(void)
{
    static const uint8_t duties[2] = { 128u, 0u };
    tfw_pinref_t pin2;
    tfw_pinref_t pin3;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(2u, &pin2));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(3u, &pin3));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin3, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin2, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_HEX32(1u << 3, model_pddr);
    for (uint8_t i = 0u; i < 2u; i++) {
        uint8_t      req[2] = { 2u, duties[i] };
        tfw_reader_t r;

        tfw_reader_init(&r, req, (uint8_t)sizeof req);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_pwm_cmd_write(&r, NULL));
        TEST_ASSERT_EQUAL_HEX32(1u << 3, model_pddr);
    }
    TEST_ASSERT_FALSE(tfw_board_pin_flags(2u) & TFW_PIN_PWM);
    TEST_ASSERT_FALSE(tfw_board_pin_flags(3u) & TFW_PIN_PWM);
}

#if TFW_IMX95_TPM_BASE != 0u
/* test_imx95_tpm_base: the board with its TPM known (a stand-in base, the
 * model's) but no way to hand a pad between the TPM and RGPIO: the System
 * Manager owns the mux (Codex P1 on #443). PWM_WRITE(128) then PWM_WRITE(0)
 * then DIGITAL_WRITE: neither PWM write may take the pad, so no TPM channel
 * is enabled and the pin's high reaches RGPIO, which still drives the pad. */
static void test_pwm_then_digital_write_leaves_the_pad_on_rgpio(void)
{
    static const uint8_t duties[2] = { 128u, 0u };
    tfw_pinref_t pin2;
    tfw_pwmref_t ch;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(2u, &pin2));
    for (uint8_t i = 0u; i < 2u; i++) {
        uint8_t      req[2] = { 2u, duties[i] };
        tfw_reader_t r;

        tfw_reader_init(&r, req, (uint8_t)sizeof req);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_pwm_cmd_write(&r, NULL));
    }
    ch.unit    = 0u;
    ch.channel = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_write(&ch, 128u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_pwm_stop(&ch));
    for (uint8_t c = 0u; c < MODEL_CHANS; c++) {
        TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_IMX95_TPM_CnSC(c)));
    }
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_IMX95_TPM_SC));
    TEST_ASSERT_FALSE(tfw_board_pin_flags(2u) & TFW_PIN_PWM);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin2, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&pin2, 1u));
    TEST_ASSERT_EQUAL_HEX32(1u << 2, model_pddr);
    TEST_ASSERT_EQUAL_HEX32(1u << 2, model_psor);
    TEST_ASSERT_EQUAL_UINT(0u, model_strays);
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tpm_duty_half);
    RUN_TEST(test_tpm_period_for_several_frequencies);
    RUN_TEST(test_tpm_cnv_for_several_duties);
    RUN_TEST(test_tpm_duty_0_is_low_and_255_is_high);
    RUN_TEST(test_tpm_second_channel_leaves_the_counter_alone);
    RUN_TEST(test_tpm_stop_disables_the_channel_only);
    RUN_TEST(test_tpm_a_pin_with_no_channel_is_range);
    RUN_TEST(test_pwm_write_refused_leaves_the_pin_an_input);
#if TFW_IMX95_TPM_BASE == 0u
    RUN_TEST(test_tpm_base_zero_is_unsupported);
#else
    RUN_TEST(test_pwm_then_digital_write_leaves_the_pad_on_rgpio);
#endif
    return UNITY_END();
}
