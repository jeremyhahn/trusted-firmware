/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2-tz SoC's peripherals this port drives, at their Secure aliases
 * (the core is Secure and the SAU is off): Arm's AN505 FPGA image as QEMU
 * models it (hw/arm/mps2-tz.c, hw/arm/armsse.c). Included by the
 * application's SoC files beside it and by tfw-boot's
 * (boot/hal/cortex-m/mps2-tz/).
 *
 *   UART0    a CMSDK APB UART, the byte link; its two interrupt lines are
 *            external interrupts 32 and 33
 *   FPGAIO   the FPGA I/O block, whose LED register drives the two user LEDs
 *   WDOG     the IoT Kit's Secure CMSDK watchdog: interrupt (NMI) on the
 *            first expiry, reset on the second
 */
#ifndef TFW_CM_MPS2_TZ_REGS_H
#define TFW_CM_MPS2_TZ_REGS_H

#include "cm_regs.h"

/* ---- UART0, CMSDK APB UART */
#define TFW_MPS2_UART0_BASE    0x40200000u
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

#define TFW_MPS2_UART0_IRQ_A 32u
#define TFW_MPS2_UART0_IRQ_B 33u

/* ---- FPGA I/O block */
#define TFW_MPS2_FPGAIO_LED    TFW_CM_REG(0x40302000u)
#define TFW_MPS2_LED_COUNT     2u

/* ---- the Secure watchdog, CMSDK APB watchdog */
#define TFW_MPS2_WDOG_BASE     0x50081000u
#define TFW_MPS2_WDOG_LOAD     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x000u)
#define TFW_MPS2_WDOG_CTRL     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x008u)
#define TFW_MPS2_WDOG_INTCLR   TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0x00Cu)
#define TFW_MPS2_WDOG_LOCK     TFW_CM_REG(TFW_MPS2_WDOG_BASE + 0xC00u)

#define TFW_MPS2_WDOG_UNLOCK   0x1ACCE551u
#define TFW_MPS2_WDOG_CTRL_INTEN 0x1u
#define TFW_MPS2_WDOG_CTRL_RESEN 0x2u

#endif /* TFW_CM_MPS2_TZ_REGS_H */
