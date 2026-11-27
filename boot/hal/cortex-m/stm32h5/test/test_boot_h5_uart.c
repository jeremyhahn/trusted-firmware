/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's STM32H5 UART (boot/hal/cortex-m/stm32h5/boot_uart.c) under the
 * STM32H573I-DK's profile, over the register file the application's UART
 * suite uses (test/model/cortex-m/regfile_model.c): every word zero until
 * written, as the part's registers reset. The suite sits beside the SoC's
 * sources because it names the SoC's registers (lint-arch-leak).
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/config.h"
#include "regfile_model.h"
#include "../../../../boot_hal.h"
#include "../../boot_soc.h"
#include "stm32h5/soc_regs.h"
#include "stm32h5/h5_calc.h"

#define VCP_BANK tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_VCP_BANK)

static uint32_t mode_of(uint32_t pin)
{
    return (TFW_H5_GPIO_MODER(VCP_BANK) >> (2u * pin)) & 0x3u;
}

static uint32_t af_of(uint32_t pin)
{
    return (TFW_H5_GPIO_AFRH(VCP_BANK) >> (4u * (pin & 7u))) & 0xFu;
}

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void)
{
}

/* A baud whose divisor works out 0 (here 0 itself) leaves every register as
 * reset: no clock, no pin, no USART. */
static void test_a_zero_divisor_writes_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_usart_brr((uint32_t)TFW_CM_CPU_HZ, 0u));
    tfw_boot_uart_init(0u);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_RCC_AHB2ENR);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_RCC_APB2ENR);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_GPIO_MODER(VCP_BANK));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_GPIO_AFRH(VCP_BANK));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_BRR);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_CR1);
}

/* Init clocks GPIOA and USART1, writes the divisor, and enables the USART
 * with its receiver and transmitter and nothing else: no interrupt. */
static void test_init_sets_cr1_and_brr(void)
{
    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_HEX32(1u << TFW_H5_VCP_BANK, TFW_H5_RCC_AHB2ENR);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RCC_APB2ENR_USART1EN, TFW_H5_RCC_APB2ENR);
    TEST_ASSERT_EQUAL_HEX32(tfw_h5_usart_brr((uint32_t)TFW_CM_CPU_HZ, 115200u), TFW_H5_USART_BRR);
    TEST_ASSERT_NOT_EQUAL(0u, TFW_H5_USART_BRR);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE | TFW_H5_USART_CR1_TE,
                            TFW_H5_USART_CR1);
}

/* PA9 (TX) and PA10 (RX) go to alternate function 7; their neighbours stay
 * as reset. */
static void test_init_puts_the_pins_on_their_alternate_function(void)
{
    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(TFW_H5_VCP_RX_PIN));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_VCP_AF, af_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_VCP_AF, af_of(TFW_H5_VCP_RX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, mode_of(8u));
    TEST_ASSERT_EQUAL_UINT32(0u, mode_of(11u));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(8u));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(11u));
}

/* An overrun with no byte waiting: rx clears it, so reception goes on, and
 * reports nothing, leaving the caller's byte alone. */
static void test_rx_clears_an_overrun_and_returns_0_with_no_byte(void)
{
    uint8_t b = 0x5Au;

    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_ORE;
    TFW_H5_USART_RDR = 0xA5u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, TFW_H5_USART_ICR);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
}

/* With a byte waiting it is RDR's; no overrun means no ICR write. */
static void test_rx_takes_rdr_with_rxne_set(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_RXNE;
    TFW_H5_USART_RDR = 0xA5u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_ICR);
}

/* An idle transmitter keeps TXE and TC set, so a byte arriving reads
 * ISR as 0xE0: rx must test RXNE alone, not the whole register. */
static void test_rx_takes_rdr_with_the_idle_flags_set(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE | TFW_H5_USART_ISR_TC |
                       TFW_H5_USART_ISR_RXNE;
    TFW_H5_USART_RDR = 0xA5u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xA5u, b);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_ICR);
}

/* An overrun that comes with a byte (and the idle TX flags) is cleared,
 * and the byte waiting in RDR is still returned. */
static void test_rx_clears_an_overrun_and_takes_the_byte_waiting(void)
{
    uint8_t b = 0u;

    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE | TFW_H5_USART_ISR_TC |
                       TFW_H5_USART_ISR_RXNE | TFW_H5_USART_ISR_ORE;
    TFW_H5_USART_RDR = 0x3Cu;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, b);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, TFW_H5_USART_ICR);
}

/* A UART init left off (TE clear) stores nothing, even with TXE set. */
static void test_tx_stores_nothing_while_te_is_clear(void)
{
    tfw_boot_uart_init(0u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE;
    tfw_boot_uart_tx(0xA5u);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_TDR);
}

/* Enabled and with TXE set, the byte goes to TDR. */
static void test_tx_writes_tdr_once_enabled(void)
{
    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE;
    tfw_boot_uart_tx(0xA5u);
    TEST_ASSERT_EQUAL_HEX32(0xA5u, TFW_H5_USART_TDR);
}

/* Seeds the registers the hand-over shares with its neighbours: another
 * bank's clock, another APB2 peripheral's clock, and PA8/PA11 as outputs on
 * alternate function 5, so a whole-register write shows. */
#define NEIGHBOUR_AHB2ENR (1u << 2u)
#define NEIGHBOUR_APB2ENR (1u << 12u)
#define NEIGHBOUR_MODER   ((0x1u << (2u * 8u)) | (0x1u << (2u * 11u)))
#define NEIGHBOUR_AFRH    ((0x5u << (4u * (8u & 7u))) | (0x5u << (4u * (11u & 7u))))

static void seed_neighbours(void)
{
    TFW_H5_RCC_AHB2ENR = NEIGHBOUR_AHB2ENR;
    TFW_H5_RCC_APB2ENR = NEIGHBOUR_APB2ENR;
    TFW_H5_GPIO_MODER(VCP_BANK) = NEIGHBOUR_MODER;
    TFW_H5_GPIO_AFRH(VCP_BANK) = NEIGHBOUR_AFRH;
}

/* The hand-over to the application puts USART1, PA9/PA10 and their clocks
 * back as a bare reset leaves them, clearing only its own bits. */
static void test_off_puts_usart1_and_its_pins_back_to_reset(void)
{
    seed_neighbours();
    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TC;
    TFW_H5_USART_ICR = 0u;
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_CR1);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_BRR);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, TFW_H5_USART_ICR);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_ANALOG, mode_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_ANALOG, mode_of(TFW_H5_VCP_RX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(TFW_H5_VCP_RX_PIN));
    TEST_ASSERT_EQUAL_UINT32(1u, mode_of(8u));
    TEST_ASSERT_EQUAL_UINT32(1u, mode_of(11u));
    TEST_ASSERT_EQUAL_UINT32(5u, af_of(8u));
    TEST_ASSERT_EQUAL_UINT32(5u, af_of(11u));
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_APB2ENR, TFW_H5_RCC_APB2ENR);
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_AHB2ENR, TFW_H5_RCC_AHB2ENR);
}

/* An init left off changed nothing, so the hand-over changes nothing either
 * and does not wait for a TC that never comes (ISR stays 0 here: without the
 * TE check this test would not finish). */
static void test_off_after_an_init_left_off_touches_nothing(void)
{
    seed_neighbours();
    TFW_H5_USART_BRR = 0x1234u;
    TFW_H5_USART_ICR = 0u;
    tfw_boot_uart_init(0u);
    tfw_cm_boot_uart_off();
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_AHB2ENR, TFW_H5_RCC_AHB2ENR);
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_APB2ENR, TFW_H5_RCC_APB2ENR);
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_MODER, TFW_H5_GPIO_MODER(VCP_BANK));
    TEST_ASSERT_EQUAL_HEX32(NEIGHBOUR_AFRH, TFW_H5_GPIO_AFRH(VCP_BANK));
    TEST_ASSERT_EQUAL_HEX32(0x1234u, TFW_H5_USART_BRR);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_CR1);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_ICR);
    TEST_ASSERT_EQUAL_UINT32(0u, mode_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, mode_of(TFW_H5_VCP_RX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(TFW_H5_VCP_TX_PIN));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(TFW_H5_VCP_RX_PIN));
}

/* After the hand-over TE is clear, so a stray tx stores nothing. */
static void test_after_off_a_stray_tx_stores_nothing(void)
{
    tfw_boot_uart_init(115200u);
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TC | TFW_H5_USART_ISR_TXE;
    tfw_cm_boot_uart_off();
    TFW_H5_USART_TDR = 0u;
    tfw_boot_uart_tx(0xA5u);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_TDR);
}

static void test_init_over_a_live_usart_writes_its_own_divisor(void)
{
    TFW_H5_USART_BRR = 0x1234u;
    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_HEX32(tfw_h5_usart_brr((uint32_t)TFW_CM_CPU_HZ, 115200u), TFW_H5_USART_BRR);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE | TFW_H5_USART_CR1_TE,
                            TFW_H5_USART_CR1);
}

static void test_init_over_a_live_usart_clears_its_interrupt_enables(void)
{
    TFW_H5_USART_CR1 = TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE | TFW_H5_USART_CR1_TE |
                       TFW_H5_USART_CR1_RXNEIE | TFW_H5_USART_CR1_TXEIE;
    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_RE | TFW_H5_USART_CR1_TE,
                            TFW_H5_USART_CR1);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_CR1 & (TFW_H5_USART_CR1_RXNEIE | TFW_H5_USART_CR1_TXEIE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_zero_divisor_writes_nothing);
    RUN_TEST(test_init_sets_cr1_and_brr);
    RUN_TEST(test_init_puts_the_pins_on_their_alternate_function);
    RUN_TEST(test_rx_clears_an_overrun_and_returns_0_with_no_byte);
    RUN_TEST(test_rx_takes_rdr_with_rxne_set);
    RUN_TEST(test_rx_takes_rdr_with_the_idle_flags_set);
    RUN_TEST(test_rx_clears_an_overrun_and_takes_the_byte_waiting);
    RUN_TEST(test_tx_stores_nothing_while_te_is_clear);
    RUN_TEST(test_tx_writes_tdr_once_enabled);
    RUN_TEST(test_off_puts_usart1_and_its_pins_back_to_reset);
    RUN_TEST(test_off_after_an_init_left_off_touches_nothing);
    RUN_TEST(test_after_off_a_stray_tx_stores_nothing);
    RUN_TEST(test_init_over_a_live_usart_writes_its_own_divisor);
    RUN_TEST(test_init_over_a_live_usart_clears_its_interrupt_enables);
    return UNITY_END();
}
