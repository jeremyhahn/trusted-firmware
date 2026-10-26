/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7 board's HAL on the host (SPEC row `imx95`): the files the M7
 * image links from src/hal/cortex-m/imx95/ -- hal_uart.c, hal_gpio.c,
 * hal_soc.c and soc_calc.c -- unchanged, against the register model in
 * test/model/imx95_model.c, which mk/host.mk substitutes for the core's
 * register header. QEMU has no i.MX 95, so until the bench has the board this
 * is the only run these paths get.
 *
 * Every register value below is a literal, worked from the block layouts
 * soc_regs.h cites (NXP's LPUART, RGPIO and WDOG32, TO CONFIRM against
 * IMX95RM, docs/multi-arch/imx95-design.md section 9.4), not from the code:
 *
 *   LPUART CTRL  TIE 0x00800000, RIE 0x00200000, TE 0x00080000, RE 0x00040000
 *   LPUART BAUD  0x19000008 at 115200 from 24 MHz: OSR 25 (x26), SBR 8
 *   WDOG32 CS    UPDATE 0x20, EN 0x80, CLK=LPO 0x100, PRES 0x1000,
 *                CMD32EN 0x2000, FLG 0x4000
 *   WDOG32 TOVAL 8 s of 32 kHz / 256 = 1000 = 0x3E8
 *   NVIC         interrupt 64 is bit 0 of the third ISER/ICPR word
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "imx95_model.h"

static struct tfw_imx95_model *m;

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

static void uart_up(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
    m = tfw_imx95_model_get();
    m->lpuart.ctrl_unmasked = 0u;
}

/* ---------------------------------------------------------------- LPUART3 */

static void test_uart_init_programs_the_divisor_and_enables(void)
{
    uart_up();
    TEST_ASSERT_EQUAL_HEX32(0x19000008u, m->lpuart.baud);
    TEST_ASSERT_EQUAL_HEX32(0x002C0000u, m->lpuart.ctrl);      /* TE | RE | RIE */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, m->lpuart.global);    /* out of reset */
    TEST_ASSERT_EQUAL_UINT32(1u, m->lpuart.resets);
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->nvic.iser[2]);
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->nvic.icpr[2]);
    TEST_ASSERT_EQUAL_HEX32(0u, m->nvic.iser[0] | m->nvic.iser[1]);
}

/* Whatever ran before left the transmitter and receiver on; BAUD must not be
 * written until the reset has turned them off. */
static void test_uart_init_resets_before_the_divisor(void)
{
    m->lpuart.ctrl = 0x000C0000u;    /* TE | RE, as a previous image left it */
    m->lpuart.baud = 0x0F00000Du;
    uart_up();
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.baud_while_enabled);
    TEST_ASSERT_EQUAL_HEX32(0x19000008u, m->lpuart.baud);
}

static void test_uart_init_clears_a_latched_overrun(void)
{
    m->lpuart.overrun = 1u;
    uart_up();
    TEST_ASSERT_EQUAL_UINT8(0u, m->lpuart.overrun);
}

static void test_uart_init_refuses_a_baud_it_cannot_make_and_touches_nothing(void)
{
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_hal_uart_init(0u));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.resets);
    TEST_ASSERT_EQUAL_HEX32(0x0F000004u, m->lpuart.baud);       /* reset value */
    TEST_ASSERT_EQUAL_HEX32(0u, m->lpuart.ctrl);
    TEST_ASSERT_EQUAL_HEX32(0u, m->nvic.iser[2]);
}

/* A transmission starts by enabling the empty-register interrupt, inside the
 * critical section, not by writing DATA. */
static void test_tx_queues_and_raises_tie_masked(void)
{
    uart_up();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x7Eu));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00AC0000u, m->lpuart.ctrl);      /* + TIE */
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.ctrl_unmasked);
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
}

static void test_the_interrupt_sends_in_order_and_drops_tie(void)
{
    static const uint8_t frame[] = { 0x7Eu, 0x01u, 0x00u, 0xFFu };
    uint32_t i;

    uart_up();
    for (i = 0u; i < sizeof frame; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(frame[i]));
    }
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(sizeof frame, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, m->lpuart.tx, sizeof frame);
    TEST_ASSERT_EQUAL_HEX32(0x002C0000u, m->lpuart.ctrl);      /* TIE off */
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.tx_dropped);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

/* STAT.TDRE clear: nothing is written and TIE stays up for the next one. */
static void test_a_busy_transmitter_holds_the_byte(void)
{
    uart_up();
    m->lpuart.busy = 1u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x55u));
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX32(0x00AC0000u, m->lpuart.ctrl);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
    m->lpuart.busy = 0u;
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX8(0x55u, m->lpuart.tx[0]);
}

/* Empty ring but the last stop bit not out (STAT.TC clear): not idle. */
static void test_tx_idle_waits_for_tc(void)
{
    uart_up();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    m->lpuart.busy = 1u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
}

/* 64 slots, one kept empty to tell full from empty. */
static void test_the_tx_ring_holds_63(void)
{
    uint32_t i;

    uart_up();
    m->lpuart.busy = 1u;
    for (i = 0u; i < 63u; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)i));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xAAu));
    m = tfw_imx95_model_get();
    m->lpuart.busy = 0u;
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(63u, m->lpuart.tx_len);
    TEST_ASSERT_EQUAL_HEX8(62u, m->lpuart.tx[62]);
}

static void test_rx_drains_data_while_rdrf(void)
{
    static const uint8_t line[] = { 0x7Eu, 0x00u, 0x80u };
    uint8_t  b = 0u;
    uint32_t i;

    uart_up();
    tfw_imx95_model_line_rx(line, sizeof line);
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(3u, m->lpuart.rx_pos);    /* DATA read three times */
    for (i = 0u; i < sizeof line; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
        TEST_ASSERT_EQUAL_HEX8(line[i], b);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_UINT32(0u, m->lpuart.tx_len);    /* no read taken as a write */
}

/* A full ring drops the byte but still reads it, so RDRF falls. */
static void test_a_full_rx_ring_drops_but_drains(void)
{
    uint8_t  line[70];
    uint8_t  b = 0u;
    uint32_t n = 0u;

    uart_up();
    memset(line, 0x3C, sizeof line);
    tfw_imx95_model_line_rx(line, sizeof line);
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(70u, m->lpuart.rx_pos);
    while (tfw_hal_uart_rx(&b) != 0u) {
        n++;
    }
    TEST_ASSERT_EQUAL_UINT32(63u, n);
}

static void test_the_interrupt_clears_an_overrun(void)
{
    uart_up();
    m->lpuart.overrun = 1u;
    tfw_imx95_model_uart_interrupt();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT8(0u, m->lpuart.overrun);
}

/* ---------------------------------------------------------------- RGPIO */

/* The board's two pins are GPIO2_IO00 and GPIO2_IO01: port 1, bank 1. */
static void test_gpio_output_drives_psor_and_pcor(void)
{
    const tfw_pinref_t pin = { .port = 1u, .bit = 0u };
    uint8_t lvl = 0xFFu;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_OUTPUT));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->gpio[1].pddr);
    TEST_ASSERT_EQUAL_UINT32(0u, m->gpio[1].pddr_unmasked);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&pin, 1u));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->gpio[1].pdor);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&pin, &lvl));
    TEST_ASSERT_EQUAL_UINT8(1u, lvl);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&pin, 0u));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0u, m->gpio[1].pdor);
    TEST_ASSERT_EQUAL_HEX32(0u, m->gpio[0].pddr | m->gpio[2].pddr);
}

static void test_gpio_input_reads_pdir(void)
{
    const tfw_pinref_t pin = { .port = 1u, .bit = 1u };
    uint8_t lvl = 0u;

    m->gpio[1].pddr = 0x00000003u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, m->gpio[1].pddr);    /* bit 0 untouched */
    m->gpio[1].pins = 0x00000002u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&pin, &lvl));
    TEST_ASSERT_EQUAL_UINT8(1u, lvl);
    m = tfw_imx95_model_get();
    m->gpio[1].pins = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&pin, &lvl));
    TEST_ASSERT_EQUAL_UINT8(0u, lvl);
}

static void test_gpio_pullup_is_unsupported_and_writes_nothing(void)
{
    const tfw_pinref_t pin = { .port = 1u, .bit = 0u };

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&pin, TFW_PIN_INPUT_PULLUP));
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0u, m->gpio[1].pddr);
}

static void test_gpio_past_the_banks_is_bad_pin(void)
{
    const tfw_pinref_t bank6 = { .port = 5u, .bit = 0u };
    const tfw_pinref_t line32 = { .port = 0u, .bit = 32u };
    uint8_t lvl = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&bank6, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&bank6, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&line32, &lvl));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&line32, TFW_PIN_OUTPUT));
}

/* ---------------------------------------------------------------- WDOG5 */

/* The reset handler's disarm: EN clear, UPDATE kept, the longest TOVAL. */
static void test_early_disarms_the_watchdog(void)
{
    tfw_imx95_model_soc_early();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u, m->wdog.unlocks);
    TEST_ASSERT_EQUAL_HEX32(0x00007120u, m->wdog.cs_written);
    TEST_ASSERT_EQUAL_HEX32(0x0000FFFFu, m->wdog.toval);
    TEST_ASSERT_EQUAL_HEX32(0u, m->wdog.cs & 0x00000080u);
    TEST_ASSERT_EQUAL_UINT8(0u, m->wdog.fixed);
    TEST_ASSERT_EQUAL_UINT32(1u, m->wdog.masked_configs);
    TEST_ASSERT_EQUAL_UINT32(0u, m->wdog.refused);
}

static void test_arm_marks_and_sets_eight_seconds(void)
{
    m->wdog.flg = 1u;
    tfw_imx95_model_watchdog_arm();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_HEX32(0x57444F47u, tfw_imx95_model_reset_mark());
    TEST_ASSERT_EQUAL_HEX32(0x000071A0u, m->wdog.cs_written);
    TEST_ASSERT_EQUAL_HEX32(0x000003E8u, m->wdog.toval);
    TEST_ASSERT_EQUAL_UINT8(0u, m->wdog.flg);
    TEST_ASSERT_EQUAL_UINT8(1u, m->wdog.rcs);
    TEST_ASSERT_EQUAL_UINT32(1u, m->wdog.masked_configs);
}

/* A configuration that leaves UPDATE set allows the next one. */
static void test_a_second_configuration_still_takes(void)
{
    tfw_imx95_model_soc_early();
    tfw_imx95_model_watchdog_arm();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(2u, m->wdog.unlocks);
    TEST_ASSERT_EQUAL_HEX32(0x000003E8u, m->wdog.toval);
    TEST_ASSERT_EQUAL_UINT32(0u, m->wdog.refused);
}

static void test_pet_is_one_refresh_key(void)
{
    tfw_imx95_model_watchdog_arm();
    tfw_imx95_model_watchdog_pet();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(1u, m->wdog.refreshes);
    TEST_ASSERT_EQUAL_UINT32(0u, m->wdog.bad_keys);
}

/* A watchdog fixed until reset ignores the unlock: the waits are bounded,
 * the writes refused, and the call returns. */
static void test_a_locked_watchdog_does_not_park_the_core(void)
{
    m->wdog.fixed = 1u;
    m->wdog.toval = 0x00001234u;
    tfw_imx95_model_watchdog_arm();
    m = tfw_imx95_model_get();
    TEST_ASSERT_EQUAL_UINT32(0u, m->wdog.unlocks);
    TEST_ASSERT_EQUAL_UINT32(2u, m->wdog.refused);
    TEST_ASSERT_EQUAL_HEX32(0x00001234u, m->wdog.toval);
}

/* ---------------------------------------------------------------- reset cause */

/* The tree reads no M7 reset-status register: the SoC hands the marker to
 * hal_early.c's decoder unchanged and returns what it decodes. */
static void test_reset_cause_is_the_marker_decoded(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_imx95_model_reset_cause(0x57444F47u));
    TEST_ASSERT_EQUAL_HEX32(0x57444F47u, tfw_imx95_model_decoded_mark());
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, tfw_imx95_model_reset_cause(0u));
    TEST_ASSERT_EQUAL_HEX32(0u, tfw_imx95_model_decoded_mark());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uart_init_programs_the_divisor_and_enables);
    RUN_TEST(test_uart_init_resets_before_the_divisor);
    RUN_TEST(test_uart_init_clears_a_latched_overrun);
    RUN_TEST(test_uart_init_refuses_a_baud_it_cannot_make_and_touches_nothing);
    RUN_TEST(test_tx_queues_and_raises_tie_masked);
    RUN_TEST(test_the_interrupt_sends_in_order_and_drops_tie);
    RUN_TEST(test_a_busy_transmitter_holds_the_byte);
    RUN_TEST(test_tx_idle_waits_for_tc);
    RUN_TEST(test_the_tx_ring_holds_63);
    RUN_TEST(test_rx_drains_data_while_rdrf);
    RUN_TEST(test_a_full_rx_ring_drops_but_drains);
    RUN_TEST(test_the_interrupt_clears_an_overrun);
    RUN_TEST(test_gpio_output_drives_psor_and_pcor);
    RUN_TEST(test_gpio_input_reads_pdir);
    RUN_TEST(test_gpio_pullup_is_unsupported_and_writes_nothing);
    RUN_TEST(test_gpio_past_the_banks_is_bad_pin);
    RUN_TEST(test_early_disarms_the_watchdog);
    RUN_TEST(test_arm_marks_and_sets_eight_seconds);
    RUN_TEST(test_a_second_configuration_still_takes);
    RUN_TEST(test_pet_is_one_refresh_key);
    RUN_TEST(test_a_locked_watchdog_does_not_park_the_core);
    RUN_TEST(test_reset_cause_is_the_marker_decoded);
    return UNITY_END();
}
