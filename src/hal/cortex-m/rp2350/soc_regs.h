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
 *   PWM         the twelve slices' control, divider, compare and wrap registers
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
#define TFW_RP_RESET_PWM        (1u << 16)
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
#define TFW_RP_FUNC_PWM         4u
#define TFW_RP_FUNC_SIO         5u
#define TFW_RP_FUNC_NULL        0x1Fu           /* FUNCSEL's reset value: no function */

#define TFW_RP_SIO_BASE         0xD0000000u
#define TFW_RP_SIO_GPIO_IN      TFW_CM_REG(TFW_RP_SIO_BASE + 0x004u)
#define TFW_RP_SIO_GPIO_OUT     TFW_CM_REG(TFW_RP_SIO_BASE + 0x010u)
#define TFW_RP_SIO_GPIO_OUT_SET TFW_CM_REG(TFW_RP_SIO_BASE + 0x018u)
#define TFW_RP_SIO_GPIO_OUT_CLR TFW_CM_REG(TFW_RP_SIO_BASE + 0x020u)
#define TFW_RP_SIO_GPIO_OE_SET  TFW_CM_REG(TFW_RP_SIO_BASE + 0x038u)
#define TFW_RP_SIO_GPIO_OE_CLR  TFW_CM_REG(TFW_RP_SIO_BASE + 0x040u)

/* ---- PWM: base §2.2 table 13; slice registers CHn_CSR, CHn_DIV, CHn_CTR,
 * CHn_CC, CHn_TOP at 0x14 per slice, §12.5.3 (PWM register list). Twelve
 * slices; GPIO n < 32 is slice (n >> 1) & 7, channel A when n is even,
 * §12.5.2. Addresses, so the atomic aliases apply as for RESETS. */
#define TFW_RP_PWM_BASE         0x400A8000u
#define TFW_RP_PWM_SLICES       12u
#define TFW_RP_PWM_CSR(n)       (TFW_RP_PWM_BASE + 0x14u * (n) + 0x00u)
#define TFW_RP_PWM_DIV(n)       (TFW_RP_PWM_BASE + 0x14u * (n) + 0x04u)
#define TFW_RP_PWM_CTR(n)       (TFW_RP_PWM_BASE + 0x14u * (n) + 0x08u)
#define TFW_RP_PWM_CC(n)        (TFW_RP_PWM_BASE + 0x14u * (n) + 0x0Cu)
#define TFW_RP_PWM_TOP(n)       (TFW_RP_PWM_BASE + 0x14u * (n) + 0x10u)
#define TFW_RP_PWM_CSR_EN       (1u << 0)       /* DIVMODE 0: free-running */
#define TFW_RP_PWM_CC_B_SHIFT   16u             /* A is CC[15:0], B CC[31:16] */

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

/* ---- I2C0, a Synopsys DW_apb_i2c (hal_bus.c). Base and offsets §12.2.17
 * table 1108; IC_CON table 1109; IC_TAR table 1110; IC_DATA_CMD table 1112;
 * IC_FS_SCL_HCNT, _LCNT tables 1115-1116; IC_RAW_INTR_STAT table 1119;
 * IC_CLR_INTR table 1122; IC_CLR_TX_ABRT table 1127; IC_CLR_STOP_DET table
 * 1130; IC_ENABLE table 1133; IC_STATUS table 1134; IC_SDA_HOLD table 1137;
 * IC_TX_ABRT_SOURCE table 1138; IC_ENABLE_STATUS table 1145; IC_FS_SPKLEN
 * table 1146. The reset line §7.5.3 table 566; I2C0 is function F3 on GPIO4
 * (SDA) and GPIO5 (SCL), §9.4 table 677, the pins pico-sdk 2.1.1's
 * pico2_w.h names PICO_DEFAULT_I2C_SDA_PIN and _SCL_PIN. ic_clk is clk_sys
 * (§12.2.11). */
#define TFW_RP_RESET_I2C0       (1u << 4)
#define TFW_RP_FUNC_I2C         3u
#define TFW_RP_I2C0_SDA_GPIO    4u
#define TFW_RP_I2C0_SCL_GPIO    5u

#define TFW_RP_I2C0_BASE        0x40090000u
#define TFW_RP_IC_CON           TFW_CM_REG(TFW_RP_I2C0_BASE + 0x000u)
#define TFW_RP_IC_TAR           TFW_CM_REG(TFW_RP_I2C0_BASE + 0x004u)
#define TFW_RP_IC_DATA_CMD      TFW_CM_REG(TFW_RP_I2C0_BASE + 0x010u)
#define TFW_RP_IC_SS_SCL_HCNT   TFW_CM_REG(TFW_RP_I2C0_BASE + 0x014u) /* table 1113 */
#define TFW_RP_IC_SS_SCL_LCNT   TFW_CM_REG(TFW_RP_I2C0_BASE + 0x018u) /* table 1114 */
#define TFW_RP_IC_FS_SCL_HCNT   TFW_CM_REG(TFW_RP_I2C0_BASE + 0x01Cu)
#define TFW_RP_IC_FS_SCL_LCNT   TFW_CM_REG(TFW_RP_I2C0_BASE + 0x020u)
#define TFW_RP_IC_INTR_MASK     TFW_CM_REG(TFW_RP_I2C0_BASE + 0x030u)
#define TFW_RP_IC_RAW_INTR_STAT TFW_CM_REG(TFW_RP_I2C0_BASE + 0x034u)
#define TFW_RP_IC_CLR_INTR      TFW_CM_REG(TFW_RP_I2C0_BASE + 0x040u)
#define TFW_RP_IC_CLR_TX_ABRT   TFW_CM_REG(TFW_RP_I2C0_BASE + 0x054u)
#define TFW_RP_IC_CLR_STOP_DET  TFW_CM_REG(TFW_RP_I2C0_BASE + 0x060u)
#define TFW_RP_IC_ENABLE        TFW_CM_REG(TFW_RP_I2C0_BASE + 0x06Cu)
#define TFW_RP_IC_STATUS        TFW_CM_REG(TFW_RP_I2C0_BASE + 0x070u)
#define TFW_RP_IC_SDA_HOLD      TFW_CM_REG(TFW_RP_I2C0_BASE + 0x07Cu)
#define TFW_RP_IC_TX_ABRT_SOURCE TFW_CM_REG(TFW_RP_I2C0_BASE + 0x080u)
#define TFW_RP_IC_ENABLE_STATUS TFW_CM_REG(TFW_RP_I2C0_BASE + 0x09Cu)
#define TFW_RP_IC_FS_SPKLEN     TFW_CM_REG(TFW_RP_I2C0_BASE + 0x0A0u)

/* IC_CON (table 1109): master on, slave off (both required, §12.2.10.2.1),
 * RESTART on, 7-bit addressing. Init ORs in the SPEED: 1 standard mode,
 * 2 fast or fast plus. */
#define TFW_RP_IC_CON_MASTER_MODE   (1u << 0)
#define TFW_RP_IC_CON_SPEED_STD     (1u << 1)
#define TFW_RP_IC_CON_SPEED_FAST    (2u << 1)
#define TFW_RP_IC_CON_SPEED_MASK    (3u << 1)
#define TFW_RP_IC_CON_RESTART_EN    (1u << 5)
#define TFW_RP_IC_CON_SLAVE_DISABLE (1u << 6)
#define TFW_RP_IC_CON_MASTER \
    (TFW_RP_IC_CON_MASTER_MODE | \
     TFW_RP_IC_CON_RESTART_EN | TFW_RP_IC_CON_SLAVE_DISABLE)

#define TFW_RP_IC_DATA_CMD_CMD_READ (1u << 8)
#define TFW_RP_IC_DATA_CMD_STOP     (1u << 9)
#define TFW_RP_IC_DATA_CMD_RESTART  (1u << 10)

#define TFW_RP_IC_ENABLE_ENABLE     (1u << 0)
#define TFW_RP_IC_ENABLE_ABORT      (1u << 1)
#define TFW_RP_IC_ENABLE_STATUS_EN  (1u << 0)

#define TFW_RP_IC_STATUS_TFNF         (1u << 1)
#define TFW_RP_IC_STATUS_TFE          (1u << 2)
#define TFW_RP_IC_STATUS_RFNE         (1u << 3)
#define TFW_RP_IC_STATUS_MST_ACTIVITY (1u << 5)

#define TFW_RP_IC_INTR_TX_ABRT      (1u << 6)
#define TFW_RP_IC_INTR_STOP_DET     (1u << 9)

/* The aborts that are a NACK: 7-bit address (0), 10-bit address bytes 1
 * and 2 (1, 2), data (3), general call (4). Bit 12 is ARB_LOST. */
#define TFW_RP_IC_ABRT_7B_ADDR_NOACK (1u << 0)
#define TFW_RP_IC_ABRT_TXDATA_NOACK  (1u << 3)
#define TFW_RP_IC_ABRT_NOACK         0x1Fu
#define TFW_RP_IC_ABRT_ARB_LOST      (1u << 12)

#endif /* TFW_CM_RP2350_REGS_H */
