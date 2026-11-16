/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 peripherals the M7 port drives, at the addresses the M7 sees.
 * Every address, interrupt number and bit is TO CONFIRM against NXP IMX95RM
 * ("i.MX 95 Applications Processor Reference Manual"), which this project does
 * not hold: the addresses are those NXP's i.MX 95 device trees in Linux give
 * the same blocks, the register layouts those of NXP's LPUART, RGPIO and
 * WDOG32 blocks as other i.MX parts document them.
 *
 *   LPUART3  the byte link, the M7's debug UART on NXP's i.MX 95 boards
 *   GPIO1-5  RGPIO banks; their addresses are in soc_calc.c, which the host
 *            tests link
 *   WDOG5    the watchdog the System Manager configuration gives the M7
 *
 * Clocks, power, resets and pin mux are the System Manager's (SCMI, on the
 * M33): this port programs none of them.
 */
#ifndef TFW_CM_IMX95_REGS_H
#define TFW_CM_IMX95_REGS_H

#include "cm_regs.h"
#include "soc_calc.h"

/* ---- LPUART3 */
/* Base confirmed on the board (2026-11-14, live device tree, A55 view):
 * serial@42570000, the third LPUART; docs/development/bench/frdm-imx95.md.
 * The offsets below stay TO CONFIRM. */
#define TFW_IMX95_LPUART3_BASE  0x42570000u
#define TFW_IMX95_LPUART_GLOBAL TFW_CM_REG(TFW_IMX95_LPUART3_BASE + 0x08u)
#define TFW_IMX95_LPUART_BAUD   TFW_CM_REG(TFW_IMX95_LPUART3_BASE + 0x10u)
#define TFW_IMX95_LPUART_STAT   TFW_CM_REG(TFW_IMX95_LPUART3_BASE + 0x14u)
#define TFW_IMX95_LPUART_CTRL   TFW_CM_REG(TFW_IMX95_LPUART3_BASE + 0x18u)
#define TFW_IMX95_LPUART_DATA   TFW_CM_REG(TFW_IMX95_LPUART3_BASE + 0x1Cu)

#define TFW_IMX95_LPUART_GLOBAL_RST 0x00000002u
#define TFW_IMX95_LPUART_STAT_TDRE  0x00800000u
#define TFW_IMX95_LPUART_STAT_TC    0x00400000u
#define TFW_IMX95_LPUART_STAT_RDRF  0x00200000u
#define TFW_IMX95_LPUART_STAT_OR    0x00080000u   /* write one to clear */
#define TFW_IMX95_LPUART_CTRL_TIE   0x00800000u
#define TFW_IMX95_LPUART_CTRL_RIE   0x00200000u
#define TFW_IMX95_LPUART_CTRL_TE    0x00080000u
#define TFW_IMX95_LPUART_CTRL_RE    0x00040000u

/* The board's device tree gives serial@42570000 GIC SPI 64 (A55). That the
 * M7's NVIC line is the same number stays TO CONFIRM. */
#define TFW_IMX95_LPUART3_IRQ 64u

/* ---- RGPIO: the port registers, offsets from a bank's base */
#define TFW_IMX95_GPIO_PSOR 0x44u   /* set */
#define TFW_IMX95_GPIO_PCOR 0x48u   /* clear */
#define TFW_IMX95_GPIO_PDIR 0x50u   /* input level */
#define TFW_IMX95_GPIO_PDDR 0x54u   /* direction, 1 = output */
#define TFW_IMX95_GPIO_REG(base, off) TFW_CM_REG((base) + (off))

/* ---- WDOG5, a WDOG32 */
/* TO CONFIRM: the stock image's device tree has no node at 0x424B0000 (its
 * only watchdog is 0x42490000), so the board read neither confirms nor
 * refutes WDOG5 here. */
#define TFW_IMX95_WDOG5_BASE  0x424B0000u
#define TFW_IMX95_WDOG_CS     TFW_CM_REG(TFW_IMX95_WDOG5_BASE + 0x0u)
#define TFW_IMX95_WDOG_CNT    TFW_CM_REG(TFW_IMX95_WDOG5_BASE + 0x4u)
#define TFW_IMX95_WDOG_TOVAL  TFW_CM_REG(TFW_IMX95_WDOG5_BASE + 0x8u)

#define TFW_IMX95_WDOG_UNLOCK  0xD928C520u
#define TFW_IMX95_WDOG_REFRESH 0xB480A602u
#define TFW_IMX95_WDOG_CS_UPDATE  0x00000020u
#define TFW_IMX95_WDOG_CS_EN      0x00000080u
#define TFW_IMX95_WDOG_CS_CLK_LPO 0x00000100u   /* CLK = 01, the 32 kHz LPO */
#define TFW_IMX95_WDOG_CS_RCS     0x00000400u
#define TFW_IMX95_WDOG_CS_ULK     0x00000800u
#define TFW_IMX95_WDOG_CS_PRES    0x00001000u   /* divide by 256 */
#define TFW_IMX95_WDOG_CS_CMD32EN 0x00002000u
#define TFW_IMX95_WDOG_CS_FLG     0x00004000u   /* write one to clear */
#define TFW_IMX95_WDOG_LPO_HZ     32000u

#endif /* TFW_CM_IMX95_REGS_H */
