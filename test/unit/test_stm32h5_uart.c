/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 byte link's register values (src/hal/cortex-m/stm32h5/hal_uart.c)
 * through tfw_hal_uart_init, tfw_hal_uart_tx_try, tfw_hal_uart_tx_idle,
 * tfw_hal_uart_rx and the USART1 service routine on the register file
 * (test/model/cortex-m/regfile_model.c): USART1's BRR for 115200 at 64 MHz,
 * CR1's UE/TE/RE, PA9 and PA10 on alternate function 7, TDR written from the
 * ring with TXE set and not with it clear, tx_idle on TC, and RDR into the
 * ring with RXNE set and nothing with it clear. The register file holds what
 * the test writes, so each status flag the HAL polls is preset before the
 * call. A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "regfile_model.h"

/* hal_uart.c's USART1 handler (USART1_IRQn 58): the vector table names it. */
void tfw_cm_irq58(void);

#define GPIOA  tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, 0u)

/* 64 MHz / 115200 = 555.6, rounded to 556 (RM0481 USART baud rate, oversampling
 * by 16: BRR = USARTDIV = f_ck / baud). */
#define BRR_115200_AT_64MHZ 556u

/* The TX ring holds one less than its 64 slots. */
#define TX_RING_ROOM 63u

static uint32_t mode_of(uint8_t pin)
{
    return (TFW_H5_GPIO_MODER(GPIOA) >> (2u * pin)) & 0x3u;
}

static uint32_t af_of(uint8_t pin)
{
    return (TFW_H5_GPIO_AFRH(GPIOA) >> (4u * (pin & 7u))) & 0xFu;
}

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

static void test_the_clock_is_64mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(64000000u, (uint32_t)TFW_CM_CPU_HZ);
}

static void test_brr_for_115200_at_64mhz(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(BRR_115200_AT_64MHZ, TFW_H5_USART_BRR);
}

static void test_a_baud_out_of_range_writes_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_uart_init(64000000u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek((uintptr_t)TFW_H5_USART1_BASE + 0x00u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek((uintptr_t)TFW_H5_USART1_BASE + 0x0Cu));
}

static void test_cr1_enables_the_usart_transmitter_and_receiver(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_UE | TFW_H5_USART_CR1_TE | TFW_H5_USART_CR1_RE |
                            TFW_H5_USART_CR1_RXNEIE, TFW_H5_USART_CR1);
    TEST_ASSERT_EQUAL_HEX32(0x1u, TFW_H5_USART_CR1 & 0x1u);   /* UE, bit 0 */
    TEST_ASSERT_EQUAL_HEX32(0x4u, TFW_H5_USART_CR1 & 0x4u);   /* RE, bit 2 */
    TEST_ASSERT_EQUAL_HEX32(0x8u, TFW_H5_USART_CR1 & 0x8u);   /* TE, bit 3 */
}

static void test_init_clears_an_overrun_and_clocks_usart1(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_ICR_ORECF, TFW_H5_USART_ICR);
    TEST_ASSERT_EQUAL_HEX32(0x1u << 14, TFW_H5_RCC_APB2ENR & (0x1u << 14));   /* USART1EN */
}

static void test_init_enables_usart1s_interrupt(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(0x1u << (58u - 32u), TFW_CM_NVIC_ISER(1u));
}

static void test_the_pins_are_pa9_and_pa10_on_af7(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_HEX32(0x1u, TFW_H5_RCC_AHB2ENR & 0x1u);   /* GPIOAEN */
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(9u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(10u));
    TEST_ASSERT_EQUAL_UINT32(7u, af_of(9u));
    TEST_ASSERT_EQUAL_UINT32(7u, af_of(10u));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(8u));
    TEST_ASSERT_EQUAL_UINT32(0u, af_of(11u));
}

static void test_tx_try_with_txe_set_writes_tdr(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_TXEIE, TFW_H5_USART_CR1 & TFW_H5_USART_CR1_TXEIE);

    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_HEX32(0xA5u, TFW_H5_USART_TDR);

    /* The ring is empty: the next TXE turns the interrupt off. */
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_USART_CR1 & TFW_H5_USART_CR1_TXEIE);
    TEST_ASSERT_EQUAL_HEX32(0xA5u, TFW_H5_USART_TDR);
}

static void test_tx_with_txe_clear_writes_no_tdr(void)
{
    init_115200();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xA5u));
    TFW_H5_USART_ISR = 0u;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek((uintptr_t)TFW_H5_USART1_BASE + 0x28u));
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_USART_CR1_TXEIE, TFW_H5_USART_CR1 & TFW_H5_USART_CR1_TXEIE);
}

static void test_tx_try_refuses_while_txe_stays_clear_and_the_ring_fills(void)
{
    uint32_t i;

    init_115200();
    TFW_H5_USART_ISR = 0u;
    for (i = 0u; i < TX_RING_ROOM; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try((uint8_t)i));
        tfw_cm_irq58();
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_try(0xEEu));

    /* TXE frees a slot, and the first byte queued goes out first. */
    TFW_H5_USART_TDR = 0xFFu;
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_HEX32(0x00u, TFW_H5_USART_TDR);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0xEEu));
}

static void test_tx_idle_on_tc_with_the_ring_empty(void)
{
    init_115200();
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TC;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TXE;
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
}

static void test_tx_idle_waits_for_the_ring(void)
{
    init_115200();
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TC;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_try(0x42u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());

    TFW_H5_USART_ISR = TFW_H5_USART_ISR_TC | TFW_H5_USART_ISR_TXE;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

static void test_rx_takes_rdr_with_rxne_set(void)
{
    uint8_t b = 0u;

    init_115200();
    TFW_H5_USART_RDR = 0x5Au;
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_RXNE;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

static void test_rx_has_nothing_with_rxne_clear(void)
{
    uint8_t b = 0x11u;

    init_115200();
    TFW_H5_USART_RDR = 0x5Au;
    TFW_H5_USART_ISR = 0u;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x11u, b);
}

static void test_an_overrun_is_cleared_by_the_service_routine(void)
{
    init_115200();
    TFW_H5_USART_ICR = 0u;
    TFW_H5_USART_ISR = TFW_H5_USART_ISR_ORE;
    tfw_cm_irq58();
    TEST_ASSERT_EQUAL_HEX32(0x1u << 3, TFW_H5_USART_ICR);   /* ORECF */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_clock_is_64mhz);
    RUN_TEST(test_brr_for_115200_at_64mhz);
    RUN_TEST(test_a_baud_out_of_range_writes_nothing);
    RUN_TEST(test_cr1_enables_the_usart_transmitter_and_receiver);
    RUN_TEST(test_init_clears_an_overrun_and_clocks_usart1);
    RUN_TEST(test_init_enables_usart1s_interrupt);
    RUN_TEST(test_the_pins_are_pa9_and_pa10_on_af7);
    RUN_TEST(test_tx_try_with_txe_set_writes_tdr);
    RUN_TEST(test_tx_with_txe_clear_writes_no_tdr);
    RUN_TEST(test_tx_try_refuses_while_txe_stays_clear_and_the_ring_fills);
    RUN_TEST(test_tx_idle_on_tc_with_the_ring_empty);
    RUN_TEST(test_tx_idle_waits_for_the_ring);
    RUN_TEST(test_rx_takes_rdr_with_rxne_set);
    RUN_TEST(test_rx_has_nothing_with_rxne_clear);
    RUN_TEST(test_an_overrun_is_cleared_by_the_service_routine);
    return UNITY_END();
}
