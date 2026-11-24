/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RP2350 PWM: the divider helper in soc_calc.h and hal_pwm.c driven against
 * the flat register model (test/model/cortex-m/rp2350/), which stands in for
 * a PWM slice's CSR, DIV, CTR, CC and TOP and for the GPIO and SIO registers
 * the driver touches. It also drives hal_gpio.c's mode, write and read, whose
 * only host build is this binary: a bad pin, an unknown mode, the INPUT and
 * INPUT_PULLUP pads and output enable, and the SIO set, clear and input bits.
 */
#include "unity.h"
#include "tfw/hal.h"
#include "tfw/board.h"
#include "../../src/caps/pwm.h"
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

/* hal_pwm.c tfw_hal_pwm_stop clears slice_on and pin_on (lines 89, 94), so
 * the next write runs the slice setup and the pad write again (lines 43-71).
 * The model does not apply the clear alias: zero the registers by hand. */
static void test_pwm_write_after_stop_sets_the_slice_up_again(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 128u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&gp2));
    tfw_rp_model_poke(TFW_RP_PWM_CSR(1u), 0u);
    tfw_rp_model_poke(TFW_RP_PWM_DIV(1u), 0u);
    tfw_rp_model_poke(TFW_RP_PWM_TOP(1u), 0u);
    tfw_rp_model_poke(TFW_RP_PADS_GPIO(2u), 0u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_write(&gp2, 50u));
    TEST_ASSERT_EQUAL_HEX32((46u << 4) | 14u, reg(TFW_RP_PWM_DIV(1u)));
    TEST_ASSERT_EQUAL_HEX32(255u, reg(TFW_RP_PWM_TOP(1u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_PWM_CSR_EN, reg(TFW_RP_PWM_CSR(1u)));
    TEST_ASSERT_EQUAL_HEX32(tfw_rp_pad_value(0u), reg(TFW_RP_PADS_GPIO(2u)));
    TEST_ASSERT_EQUAL_HEX32(50u, reg(TFW_RP_PWM_CC(1u)) & 0xFFFFu);
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_PWM, reg(0x40028000u + 0x004u + 8u * 2u));
    tfw_hal_pwm_stop(&gp2);
}

/* hal_pwm.c tfw_hal_pwm_stop clears EN only on a slice that is on (line 87);
 * the pin still goes back to SIO, driven low (lines 91-93). */
static void test_pwm_stop_on_a_slice_never_written(void)
{
    const tfw_pwmref_t gp4 = { 2u, 0u, 0u, 4u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_pwm_stop(&gp4));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PWM_CSR(2u) + 0x3000u));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PWM_CSR(2u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_SIO, reg(0x40028000u + 0x004u + 8u * 4u));
    TEST_ASSERT_EQUAL_HEX32(1u << 4, reg(TFW_RP_SIO_BASE + 0x020u));
    TEST_ASSERT_EQUAL_HEX32(1u << 4, reg(TFW_RP_SIO_BASE + 0x038u));
}

/* The board lookups PWM_WRITE makes, for logical pin 2 = GPIO2 (slice 1,
 * channel A). The suite supplies them so the command handler runs against
 * the RP2350 HAL alone. */
uint8_t tfw_board_pin_flags(tfw_pin_t pin)
{
    (void)pin;
    return 0u;
}

tfw_status_t tfw_board_pwm(tfw_pin_t pin, tfw_pwmref_t *out)
{
    if (pin != 2u) {
        return TFW_ERR_UNSUPPORTED;
    }
    *out = gp2;
    return TFW_OK;
}

tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out)
{
    if (pin != 2u) {
        return TFW_ERR_BAD_PIN;
    }
    out->port = 0u;
    out->bit  = 2u;
    return TFW_OK;
}

static tfw_status_t pwm_cmd(uint8_t pin, uint8_t duty)
{
    uint8_t      buf[2];
    tfw_reader_t r;

    buf[0] = pin;
    buf[1] = duty;
    tfw_reader_init(&r, buf, 2u);
    return tfw_pwm_cmd_write(&r, 0);
}

/* PWM_WRITE puts the pin in SIO output mode before every write, so each
 * write must hand the pin back to the PWM slice: a second nonzero duty that
 * left FUNCSEL at SIO would park the pin at its SIO level and report OK. */
static void test_pwm_cmd_successive_writes_keep_pwm_function(void)
{
    const uintptr_t io_ctrl = 0x40028000u + 0x004u + 8u * 2u;

    TEST_ASSERT_EQUAL(TFW_OK, pwm_cmd(2u, 64u));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_PWM, reg(io_ctrl));
    TEST_ASSERT_EQUAL_HEX32(64u, reg(TFW_RP_PWM_CC(1u)) & 0xFFFFu);

    TEST_ASSERT_EQUAL(TFW_OK, pwm_cmd(2u, 200u));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_PWM, reg(io_ctrl));
    TEST_ASSERT_EQUAL_HEX32(200u, reg(TFW_RP_PWM_CC(1u)) & 0xFFFFu);

    TEST_ASSERT_EQUAL(TFW_OK, pwm_cmd(2u, 0u));
}

/* GPIO n's IO_BANK0 control register, and the SIO registers hal_gpio.c uses. */
#define IO_CTRL(n) (0x40028000u + 0x004u + 8u * (n))
#define SIO_IN      (TFW_RP_SIO_BASE + 0x004u)
#define SIO_OUT_SET (TFW_RP_SIO_BASE + 0x018u)
#define SIO_OUT_CLR (TFW_RP_SIO_BASE + 0x020u)
#define SIO_OE_SET  (TFW_RP_SIO_BASE + 0x038u)
#define SIO_OE_CLR  (TFW_RP_SIO_BASE + 0x040u)

/* hal_gpio.c is_gpio and tfw_hal_gpio_mode's BAD_PIN return (lines 28-39). */
static void test_gpio_mode_refuses_bad_pin(void)
{
    const tfw_pinref_t bank1 = { 1u, 2u };
    const tfw_pinref_t gp30 = { 0u, 30u };
    const tfw_pinref_t gp29 = { 0u, 29u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&bank1, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&gp30, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OE_SET));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(IO_CTRL(30u)));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(IO_CTRL(2u)));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&gp29, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(1u << 29, reg(SIO_OE_SET));
}

/* hal_gpio.c tfw_hal_gpio_mode's UNSUPPORTED return (lines 40-43). */
static void test_gpio_mode_refuses_unsupported_mode(void)
{
    const tfw_pinref_t gp4 = { 0u, 4u };

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                      tfw_hal_gpio_mode(&gp4, (tfw_pin_mode_t)3));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(IO_CTRL(4u)));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(TFW_RP_PADS_GPIO(4u)));
}

/* hal_gpio.c tfw_hal_gpio_mode's pad, function and OE_CLR writes (lines 45-51). */
static void test_gpio_mode_input_and_pullup(void)
{
    const tfw_pinref_t gp5 = { 0u, 5u };
    const tfw_pinref_t gp6 = { 0u, 6u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&gp5, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_HEX32(0x52u, reg(TFW_RP_PADS_GPIO(5u)));
    TEST_ASSERT_EQUAL_HEX32(TFW_RP_FUNC_SIO, reg(IO_CTRL(5u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 5, reg(SIO_OE_CLR));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OE_SET));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&gp6, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_HEX32(0x5Au, reg(TFW_RP_PADS_GPIO(6u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 6, reg(SIO_OE_CLR));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OE_SET));
}

/* hal_gpio.c tfw_hal_gpio_write (lines 56-70). */
static void test_gpio_write_sets_and_clears(void)
{
    const tfw_pinref_t gp30 = { 0u, 30u };
    const tfw_pinref_t bank1 = { 1u, 7u };
    const tfw_pinref_t gp7 = { 0u, 7u };

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&gp30, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&bank1, 0u));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OUT_SET));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OUT_CLR));

    /* Any non-zero level drives the pin high. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&gp7, 2u));
    TEST_ASSERT_EQUAL_HEX32(1u << 7, reg(SIO_OUT_SET));
    TEST_ASSERT_EQUAL_HEX32(0u, reg(SIO_OUT_CLR));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&gp7, 0u));
    TEST_ASSERT_EQUAL_HEX32(1u << 7, reg(SIO_OUT_CLR));
}

/* hal_gpio.c tfw_hal_gpio_read (lines 72-79). */
static void test_gpio_read_returns_the_pin_bit(void)
{
    const tfw_pinref_t gp30 = { 0u, 30u };
    const tfw_pinref_t gp29 = { 0u, 29u };
    const tfw_pinref_t gp28 = { 0u, 28u };
    const tfw_pinref_t gp0 = { 0u, 0u };
    uint8_t level = 0xAAu;

    tfw_rp_model_poke(SIO_IN, (1u << 29) | 1u);

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&gp30, &level));
    TEST_ASSERT_EQUAL_HEX8(0xAAu, level);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&gp29, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&gp28, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&gp0, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
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
    RUN_TEST(test_pwm_write_after_stop_sets_the_slice_up_again);
    RUN_TEST(test_pwm_stop_on_a_slice_never_written);
    RUN_TEST(test_pwm_cmd_successive_writes_keep_pwm_function);
    RUN_TEST(test_gpio_mode_refuses_bad_pin);
    RUN_TEST(test_gpio_mode_refuses_unsupported_mode);
    RUN_TEST(test_gpio_mode_input_and_pullup);
    RUN_TEST(test_gpio_write_sets_and_clears);
    RUN_TEST(test_gpio_read_returns_the_pin_bit);
    return UNITY_END();
}
