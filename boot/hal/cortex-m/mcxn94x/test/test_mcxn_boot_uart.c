/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's MCXN947 bring-up of its UART, boot/hal/cortex-m/mcxn94x/boot_uart.c,
 * on the host: every SYSCON and PORT access lands in this test's register
 * model by address (boot/hal/cortex-m/rp2350/test/cm_regs.h), and the polled
 * LPUART it hands LP_FLEXCOMM4 to, mcxn_uart.c, is the test's stand-in, so
 * the test sees the unit's base, the clock and the rate, and chooses whether
 * the start succeeds. test_mcxn_uart.c covers mcxn_uart.c itself.
 */
#include <stdint.h>
#include "unity.h"
#include "boot_hal.h"
#include "cm_regs.h"
#include "mcxn_uart.h"

void tfw_cm_boot_uart_off(void);

#define AHBCLKCTRLSET0 0x50000220u
#define AHBCLKCTRLSET1 0x50000224u
#define PRESETCTRLCLR0 0x50000140u
#define PRESETCTRLCLR1 0x50000144u
#define FC4CLKSEL      0x500002C0u
#define FC4CLKDIV      0x50000860u
#define PORT1_PCR8     0x501170A0u  /* P1_8, FC4_P0, RX */
#define PORT1_PCR9     0x501170A4u  /* P1_9, FC4_P1, TX */
#define FC4_BASE       0x500B4000u

#define PORT1_CLK      (1u << 14)
#define FC4_CLK        (1u << 15)

#define MODEL_REGS 32u

static uint32_t model_addr[MODEL_REGS];
static volatile uint32_t model_val[MODEL_REGS];
static uint32_t model_used;

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    uint32_t i;

    for (i = 0u; i < model_used; i++) {
        if (model_addr[i] == addr) {
            return &model_val[i];
        }
    }
    if (model_used == MODEL_REGS) {
        TEST_FAIL_MESSAGE("register model full");
    }
    model_addr[model_used] = addr;
    model_val[model_used] = 0u;
    return &model_val[model_used++];
}

static uint32_t reg(uint32_t addr)
{
    return *tfw_cm_model_reg(addr);
}

/* What the stand-in LPUART was handed, and what it saw of SYSCON when it was
 * started. */
static tfw_status_t start_rc;
static uint32_t start_calls;
static uintptr_t start_fc;
static uint32_t start_clk_hz;
static uint32_t start_baud;
static uint32_t start_clk1;
static uint32_t start_rst1;
static uint32_t start_clksel;

tfw_status_t tfw_mcxn_uart_start(volatile uint32_t *fc, uint32_t clk_hz, uint32_t baud)
{
    start_calls++;
    start_fc = (uintptr_t)fc;
    start_clk_hz = clk_hz;
    start_baud = baud;
    start_clk1 = reg(AHBCLKCTRLSET1);
    start_rst1 = reg(PRESETCTRLCLR1);
    start_clksel = reg(FC4CLKSEL);
    return start_rc;
}

uint8_t tfw_mcxn_uart_getc(volatile uint32_t *fc, uint8_t *out)
{
    (void)fc;
    (void)out;
    return 0u;
}

void tfw_mcxn_uart_putc(volatile uint32_t *fc, uint8_t byte)
{
    (void)fc;
    (void)byte;
}

void tfw_mcxn_uart_stop(volatile uint32_t *fc)
{
    (void)fc;
}

void setUp(void)
{
    model_used = 0u;
    start_rc = TFW_OK;
    start_calls = 0u;
    start_fc = 0u;
    start_clk_hz = 0u;
    start_baud = 0u;
    start_clk1 = 0u;
    start_rst1 = 0u;
    start_clksel = 0u;
}

void tearDown(void)
{
    tfw_cm_boot_uart_off();
}

static void test_init_clocks_and_releases_fc4_before_it_starts(void)
{
    tfw_boot_uart_init(115200u);

    TEST_ASSERT_EQUAL_HEX32(PORT1_CLK, reg(AHBCLKCTRLSET0));
    TEST_ASSERT_EQUAL_HEX32(PORT1_CLK, reg(PRESETCTRLCLR0));
    TEST_ASSERT_EQUAL_HEX32(FC4_CLK, reg(AHBCLKCTRLSET1));
    TEST_ASSERT_EQUAL_HEX32(FC4_CLK, reg(PRESETCTRLCLR1));
    TEST_ASSERT_EQUAL_HEX32(2u, reg(FC4CLKSEL));          /* FRO12M */
    TEST_ASSERT_EQUAL_HEX32(0u, reg(FC4CLKDIV));          /* divide by 1, running */

    TEST_ASSERT_EQUAL_UINT32(1u, start_calls);
    TEST_ASSERT_EQUAL_HEX32(FC4_BASE, (uint32_t)start_fc);
    TEST_ASSERT_EQUAL_UINT32(12000000u, start_clk_hz);
    TEST_ASSERT_EQUAL_UINT32(115200u, start_baud);
    TEST_ASSERT_EQUAL_HEX32(FC4_CLK, start_clk1);
    TEST_ASSERT_EQUAL_HEX32(FC4_CLK, start_rst1);
    TEST_ASSERT_EQUAL_HEX32(2u, start_clksel);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_clocks_and_releases_fc4_before_it_starts);
    return UNITY_END();
}
