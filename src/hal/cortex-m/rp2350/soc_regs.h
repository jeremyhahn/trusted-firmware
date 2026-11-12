/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's peripherals this port drives (row rp2350), at the addresses
 * the RP2350 datasheet's address map gives them ([RP-DS], "Address map" and
 * each block's register list; docs/reference/README.md has its title and
 * URL). Every value was checked against [RP-DS] release 8 (build
 * 80d627281ed5); the section beside each group is that release's. The core runs Secure with the SAU off, as the boot ROM leaves an Arm
 * image, so every address is the one block's only alias. Included by the
 * application's SoC files beside it and by tfw-boot's
 * (boot/hal/cortex-m/rp2350/).
 *
 *   RESETS      takes a block out of reset
 *   CLOCKS      clk_ref, clk_sys, clk_peri
 *   XOSC        the crystal oscillator, 12 MHz on the Pico 2 W
 *   TICKS       the 1 us tick the watchdog counts
 *   WATCHDOG    the system watchdog; PSM.WDSEL says what it resets
 *   IO_BANK0    a GPIO's function select
 *   PADS_BANK0  a GPIO's pad: input enable, pulls, isolation
 *   SIO         the single-cycle GPIO set, clear and output-enable registers
 *   UART0       an Arm PL011, the byte link, on GPIO0 (TX) and GPIO1 (RX)
 *
 * Every block on the APB has the atomic aliases the datasheet describes at
 * +0x1000 (XOR), +0x2000 (set) and +0x3000 (clear) (§2.1.3); SIO has its own set and
 * clear registers instead.
 */
#ifndef TFW_CM_RP2350_REGS_H
#define TFW_CM_RP2350_REGS_H

#include "cm_regs.h"

#define TFW_RP_SET(addr) TFW_CM_REG((addr) + 0x2000u)
#define TFW_RP_CLR(addr) TFW_CM_REG((addr) + 0x3000u)

/* ---- RESETS: base §2.2 table 13; RESET, RESET_DONE §7.5.3 tables 566-567 */
#define TFW_RP_RESETS_BASE      0x40020000u
#define TFW_RP_RESETS_RESET     (TFW_RP_RESETS_BASE + 0x0u)
#define TFW_RP_RESETS_DONE      TFW_CM_REG(TFW_RP_RESETS_BASE + 0x8u)
#define TFW_RP_RESET_IO_BANK0   (1u << 6)
#define TFW_RP_RESET_PADS_BANK0 (1u << 9)
#define TFW_RP_RESET_UART0      (1u << 26)

/* ---- XOSC: base §2.2 table 13; CTRL, STATUS, STARTUP §8.2.8 tables 630-634 */
#define TFW_RP_XOSC_BASE        0x40048000u
#define TFW_RP_XOSC_CTRL        TFW_CM_REG(TFW_RP_XOSC_BASE + 0x00u)
#define TFW_RP_XOSC_STATUS      TFW_CM_REG(TFW_RP_XOSC_BASE + 0x04u)
#define TFW_RP_XOSC_STARTUP     TFW_CM_REG(TFW_RP_XOSC_BASE + 0x0Cu)
#define TFW_RP_XOSC_CTRL_1_15MHZ 0x00000AA0u   /* FREQ_RANGE */
#define TFW_RP_XOSC_CTRL_ENABLE  0x00FAB000u   /* ENABLE: the magic 0xFAB */
#define TFW_RP_XOSC_STATUS_STABLE 0x80000000u

/* ---- CLOCKS: each clock is CTRL, DIV, SELECTED. Base §2.2 table 13;
 * offsets and fields §8.1.7, tables 588-596. */
#define TFW_RP_CLOCKS_BASE      0x40010000u
#define TFW_RP_CLK_REF_CTRL     TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x30u)
#define TFW_RP_CLK_REF_DIV      TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x34u)
#define TFW_RP_CLK_REF_SELECTED TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x38u)
#define TFW_RP_CLK_SYS_CTRL     TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x3Cu)
#define TFW_RP_CLK_SYS_DIV      TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x40u)
#define TFW_RP_CLK_SYS_SELECTED TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x44u)
#define TFW_RP_CLK_PERI_CTRL    TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x48u)
#define TFW_RP_CLK_PERI_DIV     TFW_CM_REG(TFW_RP_CLOCKS_BASE + 0x4Cu)
#define TFW_RP_CLK_DIV_1        (1u << 16)      /* DIV.INT = 1, FRAC = 0 */
#define TFW_RP_CLK_REF_SRC_XOSC 2u              /* CTRL.SRC; SELECTED is one-hot */
#define TFW_RP_CLK_SYS_SRC_REF  0u
#define TFW_RP_CLK_PERI_ENABLE  (1u << 11)      /* AUXSRC 0: clk_sys */

/* ---- TICKS: the watchdog's tick generator. Base §2.2 table 13;
 * WATCHDOG_CTRL, WATCHDOG_CYCLES §8.5.2, tables 662-663. */
#define TFW_RP_TICKS_BASE       0x40108000u
#define TFW_RP_TICKS_WD_CTRL    TFW_CM_REG(TFW_RP_TICKS_BASE + 0x30u)
#define TFW_RP_TICKS_WD_CYCLES  TFW_CM_REG(TFW_RP_TICKS_BASE + 0x34u)
#define TFW_RP_TICKS_ENABLE     0x1u
/* TIMER0_CTRL, TIMER0_CYCLES §8.5.2, tables 656-657. */
#define TFW_RP_TICKS_TIMER0_CTRL   TFW_CM_REG(TFW_RP_TICKS_BASE + 0x18u)
#define TFW_RP_TICKS_TIMER0_CYCLES TFW_CM_REG(TFW_RP_TICKS_BASE + 0x1Cu)

/* ---- TIMER0: the 64-bit microsecond counter, counting the tick above
 * while interrupts are masked. Base §2.2 table 13 and §12.8.5; TIMERAWL,
 * the low word read without latching, §12.8.4.1 and §12.8.5 table 1349;
 * RESET.TIMER0 §7.5.3 table 566. */
#define TFW_RP_TIMER0_BASE      0x400B0000u
#define TFW_RP_TIMER0_TIMERAWL  TFW_CM_REG(TFW_RP_TIMER0_BASE + 0x28u)
#define TFW_RP_RESET_TIMER0     (1u << 23)

/* ---- WATCHDOG and the power-on state machine's WDSEL. Bases §2.2 table 13;
 * CTRL, LOAD §12.9.7, tables 1358-1360; WDSEL §7.4.4, tables 561 and 564. */
#define TFW_RP_WATCHDOG_BASE    0x400D8000u
#define TFW_RP_WATCHDOG_CTRL    TFW_CM_REG(TFW_RP_WATCHDOG_BASE + 0x00u)
#define TFW_RP_WATCHDOG_LOAD    TFW_CM_REG(TFW_RP_WATCHDOG_BASE + 0x04u)
#define TFW_RP_WATCHDOG_REASON  TFW_CM_REG(TFW_RP_WATCHDOG_BASE + 0x08u)   /* soc_reset.c */
#define TFW_RP_WATCHDOG_CTRL_ENABLE 0x40000000u
#define TFW_RP_WATCHDOG_CTRL_PAUSE  0x07000000u /* PAUSE_DBG0, _DBG1, _JTAG */
#define TFW_RP_PSM_WDSEL        TFW_CM_REG(0x40018000u + 0x08u)

/* ---- POWMAN: the chip reset's source (soc_reset.c) */
#define TFW_RP_POWMAN_BASE       0x40100000u
#define TFW_RP_POWMAN_CHIP_RESET TFW_CM_REG(TFW_RP_POWMAN_BASE + 0x2Cu)
#define TFW_RP_PSM_WDSEL_ALL    0x01FFFFFFu
#define TFW_RP_PSM_WDSEL_OSC    0x0000000Cu     /* ROSC and XOSC: left running */

/* ---- IO_BANK0, PADS_BANK0, SIO. Bases §2.2 table 13 and §2.2.5; GPIO_CTRL
 * and FUNCSEL §9.11.1 table 683 and §9.4 table 677; pads §9.11.3 tables
 * 883 and 885; SIO GPIO registers §3.1.11. */
#define TFW_RP_IO_CTRL(n)       TFW_CM_REG(0x40028000u + 0x004u + 8u * (n))
#define TFW_RP_PADS_GPIO(n)     (0x40038000u + 0x004u + 4u * (n))
#define TFW_RP_PAD_ISO          (1u << 8)
#define TFW_RP_PAD_OD           (1u << 7)
#define TFW_RP_PAD_IE           (1u << 6)
#define TFW_RP_PAD_PUE          (1u << 3)
#define TFW_RP_PAD_PDE          (1u << 2)
#define TFW_RP_FUNC_UART        2u
#define TFW_RP_FUNC_SIO         5u
#define TFW_RP_FUNC_NULL        0x1Fu           /* FUNCSEL's reset value: no function */

#define TFW_RP_SIO_BASE         0xD0000000u
#define TFW_RP_SIO_GPIO_IN      TFW_CM_REG(TFW_RP_SIO_BASE + 0x004u)
#define TFW_RP_SIO_GPIO_OUT     TFW_CM_REG(TFW_RP_SIO_BASE + 0x010u)
#define TFW_RP_SIO_GPIO_OUT_SET TFW_CM_REG(TFW_RP_SIO_BASE + 0x018u)
#define TFW_RP_SIO_GPIO_OUT_CLR TFW_CM_REG(TFW_RP_SIO_BASE + 0x020u)
#define TFW_RP_SIO_GPIO_OE_SET  TFW_CM_REG(TFW_RP_SIO_BASE + 0x038u)
#define TFW_RP_SIO_GPIO_OE_CLR  TFW_CM_REG(TFW_RP_SIO_BASE + 0x040u)

/* ---- UART0, PL011. Base §2.2 table 13; registers §12.1.8, tables
 * 1081-1089; the IRQ number §3.2 table 95. */
#define TFW_RP_UART0_BASE       0x40070000u
#define TFW_RP_UART_DR          TFW_CM_REG(TFW_RP_UART0_BASE + 0x000u)
#define TFW_RP_UART_FR          TFW_CM_REG(TFW_RP_UART0_BASE + 0x018u)
#define TFW_RP_UART_IBRD        TFW_CM_REG(TFW_RP_UART0_BASE + 0x024u)
#define TFW_RP_UART_FBRD        TFW_CM_REG(TFW_RP_UART0_BASE + 0x028u)
#define TFW_RP_UART_LCR_H       TFW_CM_REG(TFW_RP_UART0_BASE + 0x02Cu)
#define TFW_RP_UART_CR          TFW_CM_REG(TFW_RP_UART0_BASE + 0x030u)
#define TFW_RP_UART_IMSC        TFW_CM_REG(TFW_RP_UART0_BASE + 0x038u)
#define TFW_RP_UART_ICR         TFW_CM_REG(TFW_RP_UART0_BASE + 0x044u)

#define TFW_RP_UART_FR_TXFE     (1u << 7)
#define TFW_RP_UART_FR_TXFF     (1u << 5)
#define TFW_RP_UART_FR_RXFE     (1u << 4)
#define TFW_RP_UART_FR_BUSY     (1u << 3)
#define TFW_RP_UART_LCR_H_8N1_FIFO 0x70u        /* WLEN 8 bits, FEN */
#define TFW_RP_UART_CR_ON       0x301u          /* RXE, TXE, UARTEN */
#define TFW_RP_UART_IM_RX       0x50u           /* RXIM and RTIM, the timeout */
#define TFW_RP_UART_ICR_ALL     0x7FFu

#define TFW_RP_UART0_IRQ        33u
#define TFW_RP_UART0_TX_GPIO    0u
#define TFW_RP_UART0_RX_GPIO    1u

#endif /* TFW_CM_RP2350_REGS_H */
