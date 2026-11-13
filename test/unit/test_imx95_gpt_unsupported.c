/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7 tone (src/hal/cortex-m/imx95/hal_tone.c, #22) at the
 * firmware's own GPT base, zero: every call refuses and touches nothing.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "gpt.h"
#include "imx95_model.h"

/* hal_tone.c as the firmware builds it: the GPT base is TO CONFIRM and left
 * zero, so the tone refuses. mk/host.mk keeps gpt.h's register macro, so a
 * GPT access would land on the register model at address zero and count in
 * `unmodelled`; a GPIO or NVIC write would change the model from its reset
 * state. */
static const tfw_pinref_t pin = { .port = 1u, .bit = 1u };
static struct tfw_imx95_model fresh;

void setUp(void)
{
    tfw_imx95_model_reset();
    memcpy(&fresh, tfw_imx95_model_get(), sizeof fresh);
}
void tearDown(void) {}

static void assert_untouched(void)
{
    const struct tfw_imx95_model *m = tfw_imx95_model_get();

    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
    TEST_ASSERT_EQUAL_MEMORY(&fresh, m, sizeof fresh);
}

void test_tone_base_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, (uint32_t)TFW_IMX95_GPT_BASE);
}

void test_tone_start_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_start(&pin, 440u));
    assert_untouched();
}

void test_tone_zero_hz_start_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_start(&pin, 0u));
    assert_untouched();
}

void test_tone_stop_is_unsupported(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_tone_stop());
    assert_untouched();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tone_base_is_zero);
    RUN_TEST(test_tone_start_is_unsupported);
    RUN_TEST(test_tone_zero_hz_start_is_unsupported);
    RUN_TEST(test_tone_stop_is_unsupported);
    return UNITY_END();
}
