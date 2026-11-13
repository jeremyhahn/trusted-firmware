/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The FRDM-IMX95-PRO M7 board's pin table, checked on x86.
 *
 * The table is two provisional RGPIO lines, GPIO2_IO00 and GPIO2_IO01 (port
 * 1, bits 0 and 1), digital and nothing else: no pin is claimed as an LED,
 * and none offers a converter or a compare unit (boards/frdm-imx95-m7/
 * board.c).
 *
 * Runs under `make board-sweep`, against this board's own profile (PWM=0),
 * and under host-test twice more with PWM=1 (mk/host.mk): as the image is,
 * with the TPM base zero, and with a stand-in base for the pin mapping.
 * It runs once more as test_board_frdm-imx95-m7_adc, the soft:ADC=1 row
 * (TFW_CM_ADC=1), where the analog pins' assertions run, and as
 * test_board_frdm-imx95-m7_adc_pwm_base, both rows on, where the PWM pins
 * follow the analog ones.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

/* The first of the two PWM pins, after every pin main numbered: 2, or 4 in
 * the soft:ADC=1 row (boards/frdm-imx95-m7/board.c). */
#define PWM_PIN0 ((uint8_t)(TFW_PIN_COUNT - TFW_GPIO_PWM_PINS))

void setUp(void) {}
void tearDown(void) {}

static void test_the_board_is_the_next_provisional_cortex_m_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x41u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("frdm-imx95-m7", tfw_board_name());
}

static void test_the_pins_are_four_gpio2_lines(void)
{
    TEST_ASSERT_EQUAL_UINT8((TFW_CM_ADC ? 4u : 2u) + TFW_GPIO_PWM_PINS, TFW_PIN_COUNT);
    for (uint8_t n = 0u; n < 4u; n++) {
        uint8_t i = n < 2u ? n : (uint8_t)(PWM_PIN0 + n - 2u);
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(1u, r.port);
        TEST_ASSERT_EQUAL_UINT8(n, r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_LED);
    }
}

#if TFW_CM_ADC
/* The soft:ADC=1 row: the SAR ADC's inputs appended after the GPIO pins, ADC
 * only, one channel each, and the count of channels to match. */
static void test_the_analog_pins_name_their_channels(void)
{
    TEST_ASSERT_EQUAL_UINT8(2u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_INT(1, TFW_FEATURE_ANALOG);
    for (uint8_t i = 2u; i < PWM_PIN0; i++) {
        uint8_t ch = 0xFFu;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL_UINT8(i - 2u, ch);
        TEST_ASSERT_TRUE(ch < TFW_ADC_COUNT);
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_EQUAL_HEX8(TFW_PIN_ADC, tfw_board_pin_flags(i));
    }
}
#else
static void test_the_row_off_has_no_channel(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, TFW_ADC_COUNT);
    TEST_ASSERT_EQUAL_INT(0, TFW_FEATURE_ANALOG);
}
#endif

static void test_no_pin_offers_an_adc_or_a_pwm(void)
{
    for (uint8_t n = 0u; n < 4u; n++) {
        uint8_t i = n < 2u ? n : (uint8_t)(PWM_PIN0 + n - 2u);
        uint8_t ch;
        tfw_pwmref_t p;
        int pwm = TFW_FEATURE_PWM && TFW_IMX95_TPM_BASE != 0u && n >= 2u;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_ADC);
        if (!pwm) {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
        }
    }
}

/* PWM=1 (TFW_CM_PWM) with the M7's TPM known: the two PWM pins are the TPM's
 * channels 0 and 1, on their own GPIO2 lines. Run by mk/host.mk's
 * test_board_frdm-imx95-m7_pwm_base, which passes a stand-in base. */
#if TFW_FEATURE_PWM && TFW_IMX95_TPM_BASE != 0u
static void test_the_pwm_pins_carry_the_tpm_channels(void)
{
    TEST_ASSERT_EQUAL_UINT8(2u, TFW_PWM_COUNT);
    for (uint8_t i = PWM_PIN0; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pwm(i, &p));
        TEST_ASSERT_EQUAL_UINT8(0u, p.unit);
        TEST_ASSERT_EQUAL_UINT8(i - PWM_PIN0, p.channel);
        TEST_ASSERT_EQUAL_UINT8(1u, p.port);
        TEST_ASSERT_EQUAL_UINT8(i - PWM_PIN0 + 2u, p.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
    }
}
#elif TFW_FEATURE_PWM
/* PWM=1 with TFW_IMX95_TPM_BASE zero, as the image builds today (Codex P1,
 * board.c:35): the channel table is there, but no pin offers one, so
 * PWM_WRITE is refused before the pin is made an output and CAPS lists no
 * channel. Run by mk/host.mk's test_board_frdm-imx95-m7_pwm. */
static void test_no_pin_offers_the_unknown_tpm(void)
{
    TEST_ASSERT_EQUAL_UINT8(2u, TFW_PWM_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_PWM);
    }
}
#else
/* Without PWM=1 there is no channel table at all (board-sweep). */
static void test_the_pwm_table_is_empty(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, TFW_PWM_COUNT);
}
#endif

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_the_next_provisional_cortex_m_id);
    RUN_TEST(test_the_pins_are_four_gpio2_lines);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
#if TFW_FEATURE_PWM && TFW_IMX95_TPM_BASE != 0u
    RUN_TEST(test_the_pwm_pins_carry_the_tpm_channels);
#elif TFW_FEATURE_PWM
    RUN_TEST(test_no_pin_offers_the_unknown_tpm);
#else
    RUN_TEST(test_the_pwm_table_is_empty);
#endif
#if TFW_CM_ADC
    RUN_TEST(test_the_analog_pins_name_their_channels);
#else
    RUN_TEST(test_the_row_off_has_no_channel);
#endif
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    return UNITY_END();
}
