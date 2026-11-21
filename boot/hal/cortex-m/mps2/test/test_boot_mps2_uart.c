/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled UART on mps2 (boot/hal/cortex-m/mps2/boot_uart.c) run over
 * a register model of the CMSDK APB UART: DATA +0x00, STATE +0x04 (TXFULL 0,
 * RXFULL 1, TXOVR 2, RXOVR 3, write 1 to clear), CTRL +0x08 (TXEN 0, RXEN 1,
 * TXINTEN 2, RXINTEN 3, TXOVRINTEN 4, RXOVRINTEN 5), INTSTATUS/INTCLEAR +0x0C,
 * BAUDDIV +0x10, at least 16 (Arm DDI 0479B, Cortex-M System Design Kit TRM,
 * §4.3). UART0's base and the clock are the profile's: 0x40004000 at 25 MHz on
 * AN500, 0x40200000 at 20 MHz on AN505; the binary is built once under each.
 *
 * docs/cortex-m/engineering.md: tfw-boot enables no interrupt. The model holds
 * UART0's five registers and nothing else, so a file that reached for the NVIC
 * or a timer fails here; and it reads CTRL's interrupt enables on every access,
 * so an enable written and cleared again before the next access still counts.
 */
#include <stdint.h>
#include "unity.h"
#include "tfw/config.h"
#include "boot_hal.h"
#include "cm_regs.h"

/* Not in boot_hal.h: the Cortex-M boot HAL's hand-over hook. */
void tfw_cm_boot_uart_off(void);

#define UART0        ((uint32_t)TFW_MPS2_UART0_BASE)
#define R_DATA       0u
#define R_STATE      1u
#define R_CTRL       2u
#define R_INT        3u
#define R_BAUDDIV    4u
#define MODEL_REGS   5u

#define STATE_TXFULL 0x01u
#define STATE_RXFULL 0x02u
#define STATE_OVR    0x0Cu          /* TXOVR | RXOVR */
#define CTRL_TXRX    0x03u          /* TXEN | RXEN */
#define CTRL_INTEN   0x3Cu          /* TX, RX, TX overrun, RX overrun */

/* A driver that waits on TXFULL polls STATE; more accesses than this with
 * TXFULL never clearing is a wait that would never end on the part. */
#define STATE_SPIN_MAX 64u

static volatile uint32_t model_val[MODEL_REGS];
static uint8_t  model_touched[MODEL_REGS];
static uint32_t state_accesses;
static uint32_t txfull_clears_after;  /* STATE accesses until TXFULL clears; 0 never */
static uint8_t  inten_seen;

static void model_check_inten(void)
{
    if ((model_val[R_CTRL] & CTRL_INTEN) != 0u) {
        inten_seen = 1u;
    }
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    uint32_t i;

    model_check_inten();
    if (addr < UART0 || ((addr - UART0) & 3u) != 0u || (addr - UART0) / 4u >= MODEL_REGS) {
        TEST_FAIL_MESSAGE("register outside UART0");
    }
    i = (addr - UART0) / 4u;
    if (i == R_STATE) {
        state_accesses++;
        if (txfull_clears_after != 0u && --txfull_clears_after == 0u) {
            model_val[R_STATE] &= ~STATE_TXFULL;
        }
        if ((model_val[R_STATE] & STATE_TXFULL) != 0u && state_accesses > STATE_SPIN_MAX) {
            TEST_FAIL_MESSAGE("spun on TXFULL");
        }
    }
    model_touched[i] = 1u;
    return &model_val[i];
}

void setUp(void)
{
    uint32_t i;

    for (i = 0u; i < MODEL_REGS; i++) {
        model_val[i] = 0u;
        model_touched[i] = 0u;
    }
    state_accesses = 0u;
    txfull_clears_after = 0u;
    inten_seen = 0u;
}

/* Every test: no interrupt enable was ever set, the last write included. */
void tearDown(void)
{
    model_check_inten();
    TEST_ASSERT_FALSE_MESSAGE(inten_seen, "an interrupt enable was set");
}

static void test_init_sets_the_divisor_and_enables_tx_and_rx_only(void)
{
    tfw_boot_uart_init(115200u);

    TEST_ASSERT_EQUAL_UINT32(TFW_CM_CPU_HZ / 115200u, model_val[R_BAUDDIV]);
    TEST_ASSERT_EQUAL_HEX32(STATE_OVR, model_val[R_STATE]);
    TEST_ASSERT_EQUAL_HEX32(CTRL_TXRX, model_val[R_CTRL]);
    TEST_ASSERT_FALSE(model_touched[R_INT]);
}

/* The profiles' divisors, from their clocks: 217 at 25 MHz, 173 at 20 MHz. */
static void test_init_divisor_is_the_profiles(void)
{
    tfw_boot_uart_init(115200u);

#if TFW_CM_CPU_HZ == 25000000uL
    TEST_ASSERT_EQUAL_UINT32(217u, model_val[R_BAUDDIV]);
#elif TFW_CM_CPU_HZ == 20000000uL
    TEST_ASSERT_EQUAL_UINT32(173u, model_val[R_BAUDDIV]);
#else
    TEST_FAIL_MESSAGE("not an MPS2 profile's clock");
#endif
}

static void test_tx_waits_for_txfull_then_writes_data(void)
{
    tfw_boot_uart_init(115200u);
    model_val[R_STATE] |= STATE_TXFULL;
    txfull_clears_after = 3u;
    state_accesses = 0u;

    tfw_boot_uart_tx(0xA5u);

    TEST_ASSERT_EQUAL_UINT32(0u, txfull_clears_after);
    TEST_ASSERT_EQUAL_HEX32(0xA5u, model_val[R_DATA]);
}

static void test_rx_reads_data_when_rxfull(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(115200u);
    model_val[R_DATA] = 0x3Cu;
    model_val[R_STATE] |= STATE_RXFULL;

    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);
}

/* Refusal: nothing waiting. The byte is left alone and DATA is not read. */
static void test_rx_refuses_when_rxfull_is_clear(void)
{
    uint8_t b = 0x77u;

    tfw_boot_uart_init(115200u);
    model_touched[R_DATA] = 0u;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x77u, b);
    TEST_ASSERT_FALSE(model_touched[R_DATA]);
}

/* Refusal: baud 0 has no divisor. The UART is left off, the divisor and the
 * flags untouched, and a TX after it neither writes nor waits. */
static void test_init_refuses_baud_zero(void)
{
    model_val[R_CTRL] = CTRL_TXRX;          /* whatever ran before the reset */
    tfw_boot_uart_init(0u);

    TEST_ASSERT_EQUAL_HEX32(0u, model_val[R_CTRL]);
    TEST_ASSERT_FALSE(model_touched[R_BAUDDIV]);
    TEST_ASSERT_FALSE(model_touched[R_STATE]);
}

/* Refusal: a divisor under 16, the CMSDK minimum. 16 itself is taken. */
static void test_init_refuses_a_divisor_under_16(void)
{
    tfw_boot_uart_init((uint32_t)(TFW_CM_CPU_HZ / 15u));

    TEST_ASSERT_EQUAL_HEX32(0u, model_val[R_CTRL]);
    TEST_ASSERT_FALSE(model_touched[R_BAUDDIV]);

    setUp();
    tfw_boot_uart_init((uint32_t)(TFW_CM_CPU_HZ / 16u));

    TEST_ASSERT_EQUAL_UINT32(16u, model_val[R_BAUDDIV]);
    TEST_ASSERT_EQUAL_HEX32(CTRL_TXRX, model_val[R_CTRL]);
}

/* Refusal: TX on a UART init left off writes nothing and does not wait on a
 * TXFULL that would never clear. */
static void test_tx_refuses_a_uart_never_enabled(void)
{
    tfw_boot_uart_init(0u);
    model_val[R_STATE] = STATE_TXFULL;

    tfw_boot_uart_tx(0xA5u);

    TEST_ASSERT_FALSE(model_touched[R_DATA]);
    TEST_ASSERT_EQUAL_UINT32(0u, state_accesses);
}

/* The hand-over: the last byte leaves, then the UART goes back to reset with
 * its overrun and interrupt flags cleared. */
static void test_off_drains_tx_then_resets_the_uart(void)
{
    tfw_boot_uart_init(115200u);
    model_val[R_STATE] = STATE_TXFULL;
    txfull_clears_after = 3u;

    tfw_cm_boot_uart_off();

    TEST_ASSERT_EQUAL_UINT32(0u, txfull_clears_after);
    TEST_ASSERT_EQUAL_HEX32(0u, model_val[R_CTRL]);
    TEST_ASSERT_EQUAL_HEX32(0u, model_val[R_BAUDDIV]);
    TEST_ASSERT_EQUAL_HEX32(STATE_OVR, model_val[R_STATE]);
    TEST_ASSERT_EQUAL_HEX32(0x03u, model_val[R_INT]);
}

/* Refusal: the hand-over of a UART never enabled does not wait on TXFULL. */
static void test_off_does_not_wait_on_a_uart_never_enabled(void)
{
    model_val[R_STATE] = STATE_TXFULL;

    tfw_cm_boot_uart_off();

    TEST_ASSERT_EQUAL_UINT32(1u, state_accesses);   /* the clearing write */
    TEST_ASSERT_EQUAL_HEX32(0u, model_val[R_CTRL]);
    TEST_ASSERT_EQUAL_HEX32(0x03u, model_val[R_INT]);
}

/* The model itself: an enable set and cleared between two accesses is seen. */
static void test_the_model_sees_an_enable_cleared_again(void)
{
    *tfw_cm_model_reg(UART0 + 0x08u) = CTRL_TXRX | 0x08u;
    *tfw_cm_model_reg(UART0 + 0x08u) = CTRL_TXRX;

    TEST_ASSERT_TRUE(inten_seen);
    inten_seen = 0u;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_the_divisor_and_enables_tx_and_rx_only);
    RUN_TEST(test_init_divisor_is_the_profiles);
    RUN_TEST(test_tx_waits_for_txfull_then_writes_data);
    RUN_TEST(test_rx_reads_data_when_rxfull);
    RUN_TEST(test_rx_refuses_when_rxfull_is_clear);
    RUN_TEST(test_init_refuses_baud_zero);
    RUN_TEST(test_init_refuses_a_divisor_under_16);
    RUN_TEST(test_tx_refuses_a_uart_never_enabled);
    RUN_TEST(test_off_drains_tx_then_resets_the_uart);
    RUN_TEST(test_off_does_not_wait_on_a_uart_never_enabled);
    RUN_TEST(test_the_model_sees_an_enable_cleared_again);
    return UNITY_END();
}
