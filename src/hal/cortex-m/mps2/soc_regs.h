/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 SoC's peripherals this port drives: the CMSDK blocks Arm's MPS2
 * FPGA images share. Included by the application's SoC files beside it and by
 * tfw-boot's (boot/hal/cortex-m/mps2/).
 *
 *   UART0    a CMSDK APB UART, the byte link, with one interrupt line per
 *            direction
 *   FPGAIO   the FPGA I/O block, whose LED register drives the two user LEDs
 *   WDOG     a CMSDK APB watchdog: interrupt (NMI) on the first expiry, reset
 *            on the second
 *
 * The blocks and their registers are the same in every image; where each
 * sits and which interrupts it raises is the image's, so the board profile
 * states them (boards/mps2-an505, a Cortex-M33 with the IoT Kit at its Secure
 * aliases; boards/mps2-an500, a Cortex-M7 with no TrustZone):
 *
 *   TFW_MPS2_UART0_BASE                       UART0's base address
 *   TFW_MPS2_UART0_IRQ_A, TFW_MPS2_UART0_IRQ_B  its two external interrupts,
 *                                             bare decimal literals: they also
 *                                             name the handlers (below)
 *   TFW_MPS2_FPGAIO_BASE                      the FPGA I/O block, LED at +0
 *   TFW_MPS2_WDOG_BASE                        the watchdog the images arm
 *   TFW_MPS2_I2C_BASE                         the shield 0 SBCon, the I2C bus
 *                                             hal_bus.c clocks (sbcon.c)
 *   TFW_MPS2_SPI_BASE                         the shield 0 PL022, the SPI bus
 *                                             hal_bus.c drives (pl022.c)
 *
 * The SBCon is not a CMSDK block: it is the serial bus interface of Arm's
 * baseboards (DUI 0224I, 4.19), which the MPS2 images carry unchanged. Nor is
 * the PL022: it is Arm's PrimeCell SSP (DDI 0194H).
 */
#ifndef TFW_CM_MPS2_REGS_H
#define TFW_CM_MPS2_REGS_H

#include "tfw/config.h"
#include "cm_regs.h"

/* tfw_cm_irq<n>, the vector hal_early.c gives external interrupt n, for an n
 * the board profile states. Two levels, so n is expanded before it is pasted. */
#define TFW_MPS2_IRQ_HANDLER(n)  TFW_MPS2_IRQ_HANDLER_(n)
#define TFW_MPS2_IRQ_HANDLER_(n) tfw_cm_irq##n

/* ---- UART0, CMSDK APB UART */
#define TFW_MPS2_UART_DATA     TFW_CM_REG(TFW_MPS2_UART0_BASE + 0x00u)
#define TFW_MPS2_UART_STATE    TFW_CM_REG(TFW_MPS2_UART0_BASE + 0x04u)
#define TFW_MPS2_UART_CTRL     TFW_CM_REG(TFW_MPS2_UART0_BASE + 0x08u)
#define TFW_MPS2_UART_INT      TFW_CM_REG(TFW_MPS2_UART0_BASE + 0x0Cu)   /* status / clear */
#define TFW_MPS2_UART_BAUDDIV  TFW_CM_REG(TFW_MPS2_UART0_BASE + 0x10u)

#define TFW_MPS2_UART_STATE_TXFULL 0x01u
#define TFW_MPS2_UART_STATE_RXFULL 0x02u
#define TFW_MPS2_UART_STATE_TXOVR  0x04u
#define TFW_MPS2_UART_STATE_RXOVR  0x08u
#define TFW_MPS2_UART_CTRL_TXEN    0x01u
#define TFW_MPS2_UART_CTRL_RXEN    0x02u
#define TFW_MPS2_UART_CTRL_TXINT   0x04u
#define TFW_MPS2_UART_CTRL_RXINT   0x08u
#define TFW_MPS2_UART_INT_TX       0x01u
#define TFW_MPS2_UART_INT_RX       0x02u
#define TFW_MPS2_UART_BAUDDIV_MIN  16u

/* ---- FPGA I/O block */
#define TFW_MPS2_FPGAIO_LED    TFW_CM_REG(TFW_MPS2_FPGAIO_BASE + 0x00u)
/* The FPGA I/O block's push buttons, bit n button n, read-only. QEMU does not
 * model them and reads zero (hw/misc/mps2-fpgaio.c, A_BUTTON). */
#define TFW_MPS2_FPGAIO_BUTTON TFW_CM_REG(TFW_MPS2_FPGAIO_BASE + 0x08u)
#define TFW_MPS2_LED_COUNT     2u
/* MISC, whose bit 8 is the shield 0 SPI chip select (pl022.h; AN505 Table
 * 4-5, AN500 Table 3-6: +0x4C in both images). */
#define TFW_MPS2_FPGAIO_MISC   TFW_CM_REG(TFW_MPS2_FPGAIO_BASE + 0x4Cu)

/* ---- the watchdog, CMSDK APB watchdog */
#define TFW_MPS2_WDOG_LOAD     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x000u)
#define TFW_MPS2_WDOG_CTRL     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x008u)
#define TFW_MPS2_WDOG_INTCLR   TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x00Cu)
#define TFW_MPS2_WDOG_LOCK     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0xC00u)

#define TFW_MPS2_WDOG_UNLOCK   0x1ACCE551u
#define TFW_MPS2_WDOG_CTRL_INTEN 0x1u
#define TFW_MPS2_WDOG_CTRL_RESEN 0x2u

/* ---- the shield 0 SBCon, two-wire serial bus (sbcon.h; Arm DUI 0224I, 4.19).
 * SB_CONTROL is read at +0 and SB_CONTROLS written there; SB_CONTROLC at +4. */
#define TFW_MPS2_SB_CONTROL    TFW_CM_REG(TFW_MPS2_I2C_BASE + 0x0u)
#define TFW_MPS2_SB_CONTROLS   TFW_CM_REG(TFW_MPS2_I2C_BASE + 0x0u)
#define TFW_MPS2_SB_CONTROLC   TFW_CM_REG(TFW_MPS2_I2C_BASE + 0x4u)

/* ---- the shield 0 SPI, a PL022 (pl022.h; Arm DDI 0194H, Table 3-1): one
 * register at an offset pl022.h names. */
#define TFW_MPS2_SSP(off)      TFW_CM_REG(TFW_MPS2_SPI_BASE + (off))

/* The CMSDK APB dual timer's first counter, the software PWM's clock
 * (hal_pwm.c, the PWM=1 row): *Arm Cortex-M System Design Kit Technical
 * Reference Manual* (Arm DDI 0479C), the APB dual-input timer's register
 * summary and its TimerXControl bits, as QEMU's hw/timer/cmsdk-apb-dualtimer.c models
 * them (A_TIMER1LOAD .. A_TIMER1BGLOAD, R_CONTROL_*). The base and interrupt
 * are the board's (board_profile.h). Timer 2 is left alone. */
#define TFW_MPS2_DT1(off)       TFW_CM_REG(TFW_MPS2_DUALTIMER_BASE + (off))
#define TFW_MPS2_DT1_LOAD       0x00u
#define TFW_MPS2_DT1_VALUE      0x04u
#define TFW_MPS2_DT1_CONTROL    0x08u
#define TFW_MPS2_DT1_INTCLR     0x0Cu
#define TFW_MPS2_DT1_RIS        0x10u
#define TFW_MPS2_DT1_MIS        0x14u
#define TFW_MPS2_DT1_BGLOAD     0x18u

#define TFW_MPS2_DT_CTRL_ONESHOT 0x01u
#define TFW_MPS2_DT_CTRL_SIZE32  0x02u   /* 32-bit counter */
#define TFW_MPS2_DT_CTRL_INTEN   0x20u
#define TFW_MPS2_DT_CTRL_PERIODIC 0x40u  /* reload from Load at zero */
#define TFW_MPS2_DT_CTRL_EN      0x80u
/* Periodic, 32-bit, prescale 1, interrupt on, running. */
#define TFW_MPS2_DT_CTRL_RUN     (TFW_MPS2_DT_CTRL_SIZE32 | TFW_MPS2_DT_CTRL_INTEN | \
                                  TFW_MPS2_DT_CTRL_PERIODIC | TFW_MPS2_DT_CTRL_EN)

/* The software PWM's period: 256 steps, one per duty count, at 500 Hz, from
 * the dual timer's clock, the core's (TFW_CM_CPU_HZ): QEMU clocks it from the
 * system clock on both machines (hw/arm/mps2-tz.c's MAINCLK into armsse.c,
 * hw/arm/mps2.c's SYSCLK_FRQ). 156 timer ticks a step at 20 MHz. */
#define TFW_MPS2_PWM_HZ    500u
#define TFW_MPS2_PWM_STEP  ((uint32_t)(TFW_CM_CPU_HZ / (TFW_MPS2_PWM_HZ * 256uL)))

#endif /* TFW_CM_MPS2_REGS_H */
