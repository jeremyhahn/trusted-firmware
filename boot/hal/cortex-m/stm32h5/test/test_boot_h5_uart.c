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
#include "stm32h5/soc_regs.h"
#include "stm32h5/h5_calc.h"

#define VCP_BANK tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, TFW_H5_VCP_BANK)

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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_zero_divisor_writes_nothing);
    return UNITY_END();
}
