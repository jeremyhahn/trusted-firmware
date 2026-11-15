/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The i.MX 95 M7's bench step marks (TFW_CM_STEP_MARKS, hal_soc.c's
 * tfw_cm_soc_step) against the register model: each mark is one raw byte in
 * LPUART3 DATA, written with no queue and no interrupt, the transmitter turned
 * on first when nothing has, and the marks before the pad request sent again
 * after it. Built with -DTFW_CM_STEP_MARKS=1 (mk/host.mk); the release build's
 * TFW_CM_STEP is empty and test_imx95_hal checks that image's UART.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "hal_early.h"
#include "imx95_model.h"

static struct tfw_imx95_model *m;

int32_t tfw_imx95_console_route(void);
int32_t tfw_imx95_console_route(void)
{
    return 0;
}

void setUp(void)
{
    tfw_imx95_model_reset();
    tfw_imx95_model_reset_mark_clear();
    m = tfw_imx95_model_get();
}

void tearDown(void)
{
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->unmodelled);
    TEST_ASSERT_EQUAL_UINT32(0u, m->primask);
}

/* The reset handler's first mark: the transmitter is off, so the helper sets
 * the 115200 divisor and TE (not RE, not an interrupt) before DATA. */
static void test_a_mark_turns_the_transmitter_on_and_writes_data(void)
{
    tfw_cm_soc_step('R');
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x19000008u, m->lpuart.baud);
    TEST_ASSERT_EQUAL_HEX32(0x00080000u, m->lpuart.ctrl);      /* TE only */
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.tx_dropped);
    TEST_ASSERT_EQUAL_UINT32(1u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX8('R', m->lpuart.tx[0]);
    TEST_ASSERT_EQUAL_HEX32(0u, m->nvic.iser[2]);
}

/* A transmitter already on (the SM, a previous image) keeps its divisor. */
static void test_a_running_transmitter_is_left_as_it_is(void)
{
    m->lpuart.ctrl = 0x000C0000u;
    m->lpuart.baud = 0x0F00000Du;
    tfw_cm_soc_step('D');
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x0F00000Du, m->lpuart.baud);
    TEST_ASSERT_EQUAL_HEX32(0x000C0000u, m->lpuart.ctrl);
    TEST_ASSERT_EQUAL_UINT32(1u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX8('D', m->lpuart.tx[0]);
}

/* The whole boot path: R, D, M from the reset handler, then
 * tfw_hal_uart_init's P (followed by r, d, m again) and U. */
static void test_the_boot_path_sends_every_mark_and_replays_after_p(void)
{
    static const char want[] = "RDMPrdmU";

    tfw_cm_soc_step('R');
    tfw_cm_soc_step('D');
    tfw_cm_soc_step('M');
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(sizeof want - 1u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_MEMORY(want, m->lpuart.tx, sizeof want - 1u);
    TEST_ASSERT_EQUAL_HEX32(0x002C0000u, m->lpuart.ctrl);      /* the HAL's */
}

/* A trail left by an earlier boot is cleared at R: only this boot's marks
 * are replayed. */
static void test_r_clears_an_old_trail(void)
{
    tfw_cm_soc_step('R');
    tfw_cm_soc_step('D');
    tfw_cm_soc_step('R');
    tfw_cm_soc_step('P');
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(5u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_MEMORY("RDRPr", m->lpuart.tx, 5u);
}

/* A transmitter that never empties costs bounded polls, not the boot. */
static void test_a_stuck_transmitter_does_not_park_the_core(void)
{
    m->lpuart.busy = 1u;
    tfw_cm_soc_step('R');
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00080000u, m->lpuart.ctrl);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_mark_turns_the_transmitter_on_and_writes_data);
    RUN_TEST(test_a_running_transmitter_is_left_as_it_is);
    RUN_TEST(test_the_boot_path_sends_every_mark_and_replays_after_p);
    RUN_TEST(test_r_clears_an_old_trail);
    RUN_TEST(test_a_stuck_transmitter_does_not_park_the_core);
    return UNITY_END();
}
