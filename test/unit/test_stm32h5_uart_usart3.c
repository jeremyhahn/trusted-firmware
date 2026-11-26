/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The NUCLEO-H563ZI's byte link (src/hal/cortex-m/stm32h5/hal_uart.c built on
 * boards/nucleo-h563zi, TFW_H5_CONSOLE_USART3) on the register file
 * (test/model/cortex-m/regfile_model.c): USART3 at 0x40004800 clocked by
 * RCC_APB1LENR bit 18, PD8 TX and PD9 RX on alternate function 7, USART3_IRQn
 * 60 enabled and served by tfw_cm_irq60, and TDR and RDR at USART3's offsets.
 * What the HAL writes is read back at literal RM0481 addresses, not through
 * soc_console.h's macros, so a USART1 access left in the NUCLEO build fails
 * here. Nothing lands on USART1's registers. A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "soc_console.h"
#include "regfile_model.h"

/* hal_uart.c's USART3 handler (USART3_IRQn 60): the vector table names it. */
void tfw_cm_irq60(void);

/* RM0481: USART3 on APB1, RCC at 0x44020C00, GPIOD at 0x42020C00. */
#define USART3_BASE   0x40004800u
#define USART3_CR1    (USART3_BASE + 0x00u)
#define USART3_BRR    (USART3_BASE + 0x0Cu)
#define USART3_ISR    (USART3_BASE + 0x1Cu)
#define USART3_ICR    (USART3_BASE + 0x20u)
#define USART3_RDR    (USART3_BASE + 0x24u)
#define USART3_TDR    (USART3_BASE + 0x28u)
#define RCC_APB1LENR  ((uintptr_t)TFW_H5_RCC_BASE + 0x9Cu)
#define USART3EN      (0x1u << 18)
#define GPIOD         tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, 3u)
#define GPIODEN       (0x1u << 3)

/* NVIC_ISER1 (Armv8-M ARM), set-enable for IRQs 32-63. */
#define NVIC_ISER1_ADDR ((uintptr_t)0xE000E104u)

#define BRR_115200_AT_64MHZ 556u

static uint32_t peek(uint32_t addr)
{
    return regfile_model_peek((uintptr_t)addr);
}

/* The status and data the HAL reads are preset through soc_console.h's
 * registers; test_the_console_is_usart3 pins their base to USART3's. */

static void init_115200(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_uart_init(115200u));
}

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void)
{
}

static void test_the_console_is_usart3(void)
{
    TEST_ASSERT_EQUAL_HEX32(USART3_BASE, TFW_H5_CON_BASE);
    TEST_ASSERT_EQUAL_UINT32(60u, TFW_H5_CON_IRQ);
}

static void test_a_baud_out_of_range_writes_no_usart3_register(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(64000000u));   /* divisor 1, under 16 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(900u));        /* divisor 71111, over 0xFFFF */
    TEST_ASSERT_EQUAL_HEX32(0u, peek(USART3_CR1));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(USART3_BRR));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(USART3_ICR));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(RCC_APB1LENR) & USART3EN);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_RCC_AHB2ENR);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_GPIO_MODER(GPIOD));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_GPIO_AFRH(GPIOD));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(NVIC_ISER1_ADDR));
}

static void test_a_refused_baud_then_115200_initialises(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(0u));
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(BRR_115200_AT_64MHZ, peek(USART3_BRR));
}

static void test_init_clocks_usart3_on_apb1l(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(USART3EN, regfile_model_peek(RCC_APB1LENR) & USART3EN);
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_RCC_APB2ENR & (0x1u << 14));   /* not USART1EN */
}

static void test_init_programs_usart3s_brr_and_cr1(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(BRR_115200_AT_64MHZ, peek(USART3_BRR));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_TE | TFW_H5_USART_CR1_RE |
                            TFW_H5_USART_CR1_RXNEIE, peek(USART3_CR1));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, peek(USART3_ICR));
}

static void test_init_touches_no_usart1_register(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(0u, peek(TFW_H5_USART1_BASE + 0x00u));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(TFW_H5_USART1_BASE + 0x0Cu));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(TFW_H5_USART1_BASE + 0x20u));
}

static void test_init_enables_irq60_only(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(0x1u << (60u - 32u), regfile_model_peek(NVIC_ISER1_ADDR));
}

static void test_the_pins_are_pd8_and_pd9_on_af7(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(GPIODEN, TFW_H5_RCC_AHB2ENR);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, (TFW_H5_GPIO_MODER(GPIOD) >> 16) & 0x3u);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, (TFW_H5_GPIO_MODER(GPIOD) >> 18) & 0x3u);
    TEST_ASSERT_EQUAL_HEX32(0x77u, TFW_H5_GPIO_AFRH(GPIOD));   /* AF7 on pins 8 and 9 only */
}

static void test_tx_goes_out_through_usart3s_tdr_on_irq60(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_TXEIE, peek(USART3_CR1) & TFW_H5_USART_CR1_TXEIE);

    TFW_H5_CON_ISR = TFW_H5_USART_ISR_TXE;
    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_HEX32(0xA5u, peek(USART3_TDR));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(TFW_H5_USART1_BASE + 0x28u));

    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_HEX32(0u, peek(USART3_CR1) & TFW_H5_USART_CR1_TXEIE);
}

static void test_tx_with_txe_clear_writes_no_tdr(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_HEX32(0u, peek(USART3_TDR));
}

static void test_tx_idle_reads_usart3s_tc(void)
{
    init_115200();
    TFW_H5_CON_ISR = TFW_H5_USART_ISR_TC;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TFW_H5_CON_ISR = 0u;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
}

static void test_rx_takes_usart3s_rdr_on_irq60(void)
{
    uint8_t b = 0u;

    init_115200();
    TFW_H5_CON_RDR = 0x5Au;
    TFW_H5_CON_ISR = TFW_H5_USART_ISR_RXNE;
    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

static void test_rx_ignores_usart1s_rdr(void)
{
    uint8_t b = 0x11u;

    init_115200();
    TFW_H5_USART_RDR = 0x5Au;                   /* USART1's */
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_RXNE;
    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x11u, b);
}

static void test_an_overrun_is_cleared_in_usart3s_icr(void)
{
    init_115200();
    TFW_H5_CON_ICR = 0u;
    TFW_H5_CON_ISR = TFW_H5_USART_ISR_ORE;
    tfw_cm_irq60();
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, peek(USART3_ICR));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_console_is_usart3);
    RUN_TEST(test_a_baud_out_of_range_writes_no_usart3_register);
    RUN_TEST(test_a_refused_baud_then_115200_initialises);
    RUN_TEST(test_init_clocks_usart3_on_apb1l);
    RUN_TEST(test_init_programs_usart3s_brr_and_cr1);
    RUN_TEST(test_init_touches_no_usart1_register);
    RUN_TEST(test_init_enables_irq60_only);
    RUN_TEST(test_the_pins_are_pd8_and_pd9_on_af7);
    RUN_TEST(test_tx_goes_out_through_usart3s_tdr_on_irq60);
    RUN_TEST(test_tx_with_txe_clear_writes_no_tdr);
    RUN_TEST(test_tx_idle_reads_usart3s_tc);
    RUN_TEST(test_rx_takes_usart3s_rdr_on_irq60);
    RUN_TEST(test_rx_ignores_usart1s_rdr);
    RUN_TEST(test_an_overrun_is_cleared_in_usart3s_icr);
    return UNITY_END();
}
