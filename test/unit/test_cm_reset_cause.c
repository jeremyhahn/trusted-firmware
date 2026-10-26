/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M reset marker's decode (row cortex-m, #161): the table in
 * src/hal/cortex-m/hal_reset_decode.h, which both images' reset handlers use
 * (hal_early.c) and tfw-boot hands across to SYS_INFO.
 *
 * The expected causes are literals from include/tfw/types.h, the wire's
 * values: a reset the firmware asked for (AIRCR.SYSRESETREQ, RESET mode 0)
 * is TFW_RESET_SOFTWARE, 5; the watchdog's marker is TFW_RESET_WATCHDOG, 4,
 * the only cause boot/boot_request.c's gate honours; the fault handler's
 * (tfw_cm_fault) is TFW_RESET_PANIC, 6; nothing written is
 * TFW_RESET_POWER_ON, 1.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/types.h"
#include "hal_mock.h"
#include "reset_decode_model.h"

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

static void test_the_reset_request_marker_is_a_software_reset(void)
{
    TEST_ASSERT_EQUAL_UINT8(5u, (uint8_t)cm_model_reset_decode(0x52535251uL));
}

static void test_the_watchdog_marker_is_a_watchdog_reset(void)
{
    TEST_ASSERT_EQUAL_UINT8(4u, (uint8_t)cm_model_reset_decode(0x57444F47uL));
}

static void test_the_fault_marker_is_a_panic_reset(void)
{
    TEST_ASSERT_EQUAL_UINT8(6u, (uint8_t)cm_model_reset_decode(0x43494E50uL));
}

static void test_no_marker_is_power_on(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0u));
}

/* SRAM after power-on holds whatever it holds; QEMU zeroes it, a part need
 * not. A word that is neither marker, even one bit from one, is power-on: it
 * must never read as the watchdog and open the update-request gate. */
static void test_any_other_word_is_power_on(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0xFFFFFFFFuL));
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0x57444F46uL));
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0x52535250uL));
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0x474F4457uL));
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0x43494E51uL));
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8_t)cm_model_reset_decode(0x504E4943uL));
}

/* The markers hal_early.c (the request and the fault) and hal_soc.c write are the words decoded above. */
static void test_the_markers_are_the_words_the_images_write(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x57444F47uL, TFW_CM_MARK_WATCHDOG);
    TEST_ASSERT_EQUAL_HEX32(0x52535251uL, TFW_CM_MARK_RESETREQ);
    TEST_ASSERT_EQUAL_HEX32(0x43494E50uL, TFW_CM_MARK_PANIC);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_reset_request_marker_is_a_software_reset);
    RUN_TEST(test_the_watchdog_marker_is_a_watchdog_reset);
    RUN_TEST(test_the_fault_marker_is_a_panic_reset);
    RUN_TEST(test_no_marker_is_power_on);
    RUN_TEST(test_any_other_word_is_power_on);
    RUN_TEST(test_the_markers_are_the_words_the_images_write);
    return UNITY_END();
}
