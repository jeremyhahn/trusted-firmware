/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's half of what both images share (src/hal/cortex-m/
 * hal_early.h): the core clock and the independent watchdog. tfw-boot links
 * this file as well (mk/boot.mk), so the clock both images run at, and the
 * watchdog's arm and pet, exist once.
 *
 * The clock. The part leaves reset on the HSI, a 64 MHz RC oscillator, with
 * no AHB or APB prescaler and three flash wait states (ST's
 * stm32h5xx_hal_rcc.c, its "RCC specific features"); the HSI's own divider,
 * HSIDIV, is what ST's SystemInit sets to 1 (system_stm32h5xx.c), so this
 * port does the same rather than trust its reset value. 64 MHz needs three
 * wait states in VOS3, the voltage range of reset, so the wait states are
 * written first and read back, and the divider second, as the order of a
 * clock raise must be. Every bus then runs at TFW_CM_CPU_HZ: SysTick
 * (src/hal/cortex-m/hal_tick.c) and USART1, whose kernel clock at reset is
 * PCLK2 (RCC_CCIPR1 USART1SEL 0).
 *
 * The watchdog. The IWDG counts the LSI, 32 kHz typical (ST's
 * stm32h5xx_hal_conf_template.h, LSI_VALUE), down from its reload and
 * resets the system at zero; a pet reloads it. Unlike mps2-tz's, it cannot be
 * stopped once started: tfw_cm_soc_early pets it instead of disarming it, so
 * an image entered while one runs -- the application after tfw-boot armed the
 * trial's -- has a whole period from its reset handler. The period is 4 s,
 * AVR's longest, which the trial's deadline is written against
 * (boot/boot_hal.h); the LSI's spread moves it, which RM0481 states and this
 * port has not read.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "soc_reset.h"

#define HSI_HZ        64000000uL
#define LSI_HZ        32000uL
#define WDOG_PERIOD_S 4uL

_Static_assert(TFW_CM_CPU_HZ == HSI_HZ,
               "the STM32H5 runs on the undivided HSI: TFW_CM_CPU_HZ must be 64 MHz");

void tfw_cm_soc_early(void)
{
    uint8_t ws = tfw_h5_flash_ws_vos3((uint32_t)TFW_CM_CPU_HZ);

    TFW_H5_IWDG_KR = TFW_H5_IWDG_KEY_RELOAD;

    TFW_H5_FLASH_ACR = (TFW_H5_FLASH_ACR & ~TFW_H5_FLASH_ACR_LATENCY) | ws;
    while ((TFW_H5_FLASH_ACR & TFW_H5_FLASH_ACR_LATENCY) != ws) { }

    TFW_H5_RCC_CR = TFW_H5_RCC_CR & ~TFW_H5_RCC_CR_HSIDIV;
    while ((TFW_H5_RCC_CR & TFW_H5_RCC_CR_HSIDIVF) == 0u) { }
}

void tfw_cm_soc_watchdog_arm(void)
{
    uint8_t  pr = 0u;
    uint16_t rl = 0u;

    (void)tfw_h5_iwdg_timing(LSI_HZ, WDOG_PERIOD_S, &pr, &rl);
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    TFW_H5_IWDG_KR    = TFW_H5_IWDG_KEY_ENABLE;
    TFW_H5_IWDG_KR    = TFW_H5_IWDG_KEY_ACCESS;
    TFW_H5_IWDG_PR    = pr;
    TFW_H5_IWDG_RLR   = rl;
    while ((TFW_H5_IWDG_SR & TFW_H5_IWDG_SR_BUSY) != 0u) { }
    TFW_H5_IWDG_KR    = TFW_H5_IWDG_KEY_RELOAD;
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_H5_IWDG_KR = TFW_H5_IWDG_KEY_RELOAD;
}

/* RCC_RSR, decoded and cleared by soc_reset.c. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark)
{
    return tfw_h5_reset_take(&TFW_H5_RCC_RSR, mark == TFW_CM_MARK_RESETREQ ? 1u : 0u);
}
