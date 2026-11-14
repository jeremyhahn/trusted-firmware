/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l GPIO HAL (src/hal/cortex-m/nrf54l/hal_gpio.c), unchanged, on
 * the register file (test/model/cortex-m/regfile_model.c): the PIN_CNF word
 * each mode writes (DIR bit 0, INPUT bit 1, PULL bits 2-3) on each of P0, P1
 * and P2, a write as one OUTSET or OUTCLR word of the pin's bit, a read as
 * the pin's bit of IN, and a coordinate the SoC has no pin at refused with
 * nothing written. The addresses and field values are the nrfx MDK's
 * (nrf54l15_global.h for the port bases, nrf54l15_types.h for NRF_GPIO_Type
 * and GPIO_PIN_CNF_*), written out here, not taken from soc_regs.h, so a
 * slip in either file shows. A value the hardware would supply (IN, a
 * prior PIN_CNF) is preset through soc_regs.h's own GPIO macros, as
 * lint-arch-leak keeps the Cortex-M register helper out of the test tree.
 * A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "regfile_model.h"

/* NRF_P0_S_BASE, NRF_P1_S_BASE, NRF_P2_S_BASE, and pins per port (P0.00-06,
 * P1.00-16, P2.00-10). */
static const uint32_t base[3] = { 0x5010A000u, 0x500D8200u, 0x50050400u };
static const uint8_t pins[3]  = { 7u, 17u, 11u };

#define OUTSET(p)     (base[p] + 0x004u)
#define OUTCLR(p)     (base[p] + 0x008u)
#define IN(p)         (base[p] + 0x00Cu)
#define OUT(p)        (base[p] + 0x000u)
#define PIN_CNF(p, n) (base[p] + 0x080u + 4u * (uint32_t)(n))

/* GPIO_PIN_CNF fields: DIR_Output 1 at bit 0, INPUT_Connect 0 at bit 1,
 * PULL_Pullup 3 at bits 2-3; DRIVE and SENSE left 0. */
#define CNF_DIR_OUTPUT  0x1u
#define CNF_INPUT_DISC  0x2u
#define CNF_PULL_UP     0xCu
#define CNF_PULL_MASK   0xCu

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void) {}

static tfw_pinref_t pin(uint8_t port, uint8_t bit)
{
    tfw_pinref_t p = { .port = port, .bit = bit };
    return p;
}

/* Each mode on the first and last pin of each port: the one PIN_CNF word. */
static void check_mode(tfw_pin_mode_t mode, uint32_t want)
{
    for (uint8_t p = 0; p < 3u; p++) {
        uint8_t ends[2] = { 0u, (uint8_t)(pins[p] - 1u) };
        for (int i = 0; i < 2; i++) {
            tfw_pinref_t r = pin(p, ends[i]);
            regfile_model_reset();
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&r, mode));
            TEST_ASSERT_EQUAL_HEX32(want, regfile_model_peek(PIN_CNF(p, ends[i])));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTSET(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTCLR(p)));
        }
    }
}

static void test_output_is_dir_output_input_connected_no_pull(void)
{
    check_mode(TFW_PIN_OUTPUT, CNF_DIR_OUTPUT);
}

static void test_input_is_dir_input_input_connected_no_pull(void)
{
    check_mode(TFW_PIN_INPUT, 0u);
}

static void test_input_pullup_is_dir_input_input_connected_pullup(void)
{
    check_mode(TFW_PIN_INPUT_PULLUP, CNF_PULL_UP);
}

/* The input buffer stays connected (INPUT 0) in every mode, so a read of an
 * output returns the pin's level; the reset value 0x2 disconnects it. */
static void test_no_mode_disconnects_the_input_buffer(void)
{
    const tfw_pin_mode_t modes[3] = { TFW_PIN_INPUT, TFW_PIN_OUTPUT, TFW_PIN_INPUT_PULLUP };
    tfw_pinref_t r = pin(1u, 10u);
    for (int i = 0; i < 3; i++) {
        TFW_NRF_GPIO_PIN_CNF(base[1u], 10u) = CNF_INPUT_DISC;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&r, modes[i]));
        TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(1u, 10u)) & CNF_INPUT_DISC);
    }
}

/* A mode replaces the whole word: output after pull-up drops the pull. */
static void test_a_mode_replaces_the_previous_one(void)
{
    tfw_pinref_t r = pin(2u, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&r, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&r, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(CNF_DIR_OUTPUT, regfile_model_peek(PIN_CNF(2u, 4u)));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(2u, 4u)) & CNF_PULL_MASK);
}

static void test_a_mode_writes_only_its_own_pin(void)
{
    tfw_pinref_t r = pin(1u, 5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&r, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(1u, 4u)));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(1u, 6u)));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(0u, 5u)));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(2u, 5u)));
}

static void test_an_unknown_mode_is_unsupported_and_writes_nothing(void)
{
    tfw_pinref_t r = pin(0u, 3u);
    TFW_NRF_GPIO_PIN_CNF(base[0u], 3u) = CNF_INPUT_DISC;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&r, (tfw_pin_mode_t)7));
    TEST_ASSERT_EQUAL_HEX32(CNF_INPUT_DISC, regfile_model_peek(PIN_CNF(0u, 3u)));
}

/* High is one OUTSET word of the pin's bit, low one OUTCLR word; OUT itself
 * is never written, so the other pins of the port keep their level. */
static void test_write_high_is_outset_of_the_bit(void)
{
    for (uint8_t p = 0; p < 3u; p++) {
        for (uint8_t b = 0; b < pins[p]; b++) {
            tfw_pinref_t r = pin(p, b);
            regfile_model_reset();
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&r, 1u));
            TEST_ASSERT_EQUAL_HEX32(1uL << b, regfile_model_peek(OUTSET(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTCLR(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUT(p)));
        }
    }
}

static void test_write_low_is_outclr_of_the_bit(void)
{
    for (uint8_t p = 0; p < 3u; p++) {
        for (uint8_t b = 0; b < pins[p]; b++) {
            tfw_pinref_t r = pin(p, b);
            regfile_model_reset();
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&r, 0u));
            TEST_ASSERT_EQUAL_HEX32(1uL << b, regfile_model_peek(OUTCLR(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTSET(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUT(p)));
        }
    }
}

/* Any nonzero level is high. */
static void test_write_any_nonzero_level_is_high(void)
{
    tfw_pinref_t r = pin(2u, 9u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&r, 0x80u));
    TEST_ASSERT_EQUAL_HEX32(1uL << 9, regfile_model_peek(OUTSET(2u)));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTCLR(2u)));
}

/* A read is the pin's bit of IN, 0 or 1, whatever the port's other bits. */
static void test_read_is_the_bit_of_in(void)
{
    for (uint8_t p = 0; p < 3u; p++) {
        for (uint8_t b = 0; b < pins[p]; b++) {
            tfw_pinref_t r = pin(p, b);
            uint8_t level = 0xAAu;

            TFW_NRF_GPIO_IN(base[p]) = 1uL << b;
            TEST_ASSERT_EQUAL_HEX32(1uL << b, regfile_model_peek(IN(p)));
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&r, &level));
            TEST_ASSERT_EQUAL_UINT8(1u, level);

            TFW_NRF_GPIO_IN(base[p]) = ~(1u << b);
            level = 0xAAu;
            TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&r, &level));
            TEST_ASSERT_EQUAL_UINT8(0u, level);
        }
        TFW_NRF_GPIO_IN(base[p]) = 0u;
    }
}

/* A read on one port does not see another port's IN. */
static void test_read_uses_its_own_port(void)
{
    tfw_pinref_t r = pin(1u, 2u);
    uint8_t level = 0xAAu;
    TFW_NRF_GPIO_IN(base[0u]) = 0xFFFFFFFFu;
    TFW_NRF_GPIO_IN(base[2u]) = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&r, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

/* One past each port's last pin, and a port the SoC lacks: TFW_ERR_BAD_PIN
 * from all three calls, and no register written. */
static void test_a_pin_outside_the_table_is_refused(void)
{
    const tfw_pinref_t bad[4] = {
        { .port = 0u, .bit = 7u }, { .port = 1u, .bit = 17u },
        { .port = 2u, .bit = 11u }, { .port = 3u, .bit = 0u },
    };
    for (int i = 0; i < 4; i++) {
        uint8_t level = 0xAAu;
        regfile_model_reset();
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&bad[i], TFW_PIN_OUTPUT));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&bad[i], 1u));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&bad[i], 0u));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&bad[i], &level));
        TEST_ASSERT_EQUAL_UINT8(0xAAu, level);
        if (bad[i].port < 3u) {
            uint8_t p = bad[i].port;
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(PIN_CNF(p, bad[i].bit)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTSET(p)));
            TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(OUTCLR(p)));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_is_dir_output_input_connected_no_pull);
    RUN_TEST(test_input_is_dir_input_input_connected_no_pull);
    RUN_TEST(test_input_pullup_is_dir_input_input_connected_pullup);
    RUN_TEST(test_no_mode_disconnects_the_input_buffer);
    RUN_TEST(test_a_mode_replaces_the_previous_one);
    RUN_TEST(test_a_mode_writes_only_its_own_pin);
    RUN_TEST(test_an_unknown_mode_is_unsupported_and_writes_nothing);
    RUN_TEST(test_write_high_is_outset_of_the_bit);
    RUN_TEST(test_write_low_is_outclr_of_the_bit);
    RUN_TEST(test_write_any_nonzero_level_is_high);
    RUN_TEST(test_read_is_the_bit_of_in);
    RUN_TEST(test_read_uses_its_own_port);
    RUN_TEST(test_a_pin_outside_the_table_is_refused);
    return UNITY_END();
}
