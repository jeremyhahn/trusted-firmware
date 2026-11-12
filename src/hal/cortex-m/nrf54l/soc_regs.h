/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's peripherals this port drives, at their Secure aliases (the
 * core leaves reset Secure, and this slice configures no SAU): Nordic's
 * nRF54L15, from the nrfx MDK (mdk/nrf54l15_global.h for the bases,
 * mdk/nrf54l15_types.h for the offsets and fields,
 * mdk/nrf54l15_application.h for the interrupt numbers).
 *
 *   UARTE20      the byte link: a UART with EasyDMA, in the 16 MHz peripheral
 *                domain, on the DK's console pins (P1.4 TX, P1.5 RX), which
 *                the DK's debug probe bridges to USB; interrupt 198
 *   P0, P1, P2   the three GPIO ports
 *   PWM20        the PWM, in the same peripheral domain as UARTE20 and P1
 *   WDT30        the Secure-only watchdog: reset on expiry
 *   OSCILLATORS  the PLL that sets the CPU clock
 *   CLOCK        the high-frequency crystal's start task
 *   RESET        the reset reason
 *   FICR         factory information: the trim table and the part's revision
 *   RRAMC        the RRAM controller: tfw-boot's write enable
 *                (boot/hal/cortex-m/nrf54l/boot_rram.c)
 */
#ifndef TFW_CM_NRF54L_REGS_H
#define TFW_CM_NRF54L_REGS_H

#include "cm_regs.h"

/* ---- UARTE20 */
#define TFW_NRF_UARTE_BASE          0x500C6000u
#define TFW_NRF_UARTE_IRQ           198u
#define TFW_NRF_UARTE_RX_START      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x028u)   /* TASKS_DMA.RX.START */
#define TFW_NRF_UARTE_RX_STOP       TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x02Cu)   /* TASKS_DMA.RX.STOP */
#define TFW_NRF_UARTE_TX_START      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x050u)   /* TASKS_DMA.TX.START */
#define TFW_NRF_UARTE_TXDRDY        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x10Cu)   /* EVENTS_TXDRDY: a byte left TXD */
#define TFW_NRF_UARTE_ERROR         TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x114u)   /* EVENTS_ERROR */
#define TFW_NRF_UARTE_RXTO          TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x124u)   /* EVENTS_RXTO: the receiver stopped */
#define TFW_NRF_UARTE_RX_END        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x14Cu)   /* EVENTS_DMA.RX.END */
#define TFW_NRF_UARTE_TX_END        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x168u)   /* EVENTS_DMA.TX.END */
#define TFW_NRF_UARTE_INTEN         TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x300u)
#define TFW_NRF_UARTE_ERRORSRC      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x480u)   /* write 1 to clear */
#define TFW_NRF_UARTE_ENABLE        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x500u)
#define TFW_NRF_UARTE_BAUDRATE      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x524u)
#define TFW_NRF_UARTE_CONFIG        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x56Cu)
#define TFW_NRF_UARTE_PSEL_TXD      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x604u)
#define TFW_NRF_UARTE_PSEL_CTS      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x608u)
#define TFW_NRF_UARTE_PSEL_RXD      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x60Cu)
#define TFW_NRF_UARTE_PSEL_RTS      TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x610u)
#define TFW_NRF_UARTE_RX_PTR        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x704u)   /* DMA.RX.PTR */
#define TFW_NRF_UARTE_RX_MAXCNT     TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x708u)
#define TFW_NRF_UARTE_TX_PTR        TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x73Cu)   /* DMA.TX.PTR */
#define TFW_NRF_UARTE_TX_MAXCNT     TFW_CM_REG(TFW_NRF_UARTE_BASE + 0x740u)

#define TFW_NRF_UARTE_INTEN_TXDRDY  (1uL << 3)
#define TFW_NRF_UARTE_INTEN_RXEND   (1uL << 19)   /* DMARXEND */
#define TFW_NRF_UARTE_ENABLE_ON     8u
#define TFW_NRF_UARTE_CONFIG_8N1    (8uL << 9)    /* FRAMESIZE 8, no parity, one stop, no flow control */
#define TFW_NRF_UARTE_ERRORSRC_ALL  0xFu          /* OVERRUN, PARITY, FRAMING, BREAK */
#define TFW_NRF_UARTE_BAUDRATE_RESET 0x04000000uL /* the reset values tfw-boot leaves behind */
#define TFW_NRF_UARTE_CONFIG_RESET  0x00001000uL  /* (UARTE_BAUDRATE_ResetValue, UARTE_CONFIG_ResetValue) */

/* The DK's console pins (Zephyr's nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi,
 * uart20_default). */
#define TFW_NRF_CONSOLE_PORT        1u
#define TFW_NRF_CONSOLE_TX_BIT      4u
#define TFW_NRF_CONSOLE_RX_BIT      5u

/* ---- GPIO */
#define TFW_NRF_P0_BASE             0x5010A000u
#define TFW_NRF_P1_BASE             0x500D8200u
#define TFW_NRF_P2_BASE             0x50050400u
#define TFW_NRF_GPIO_OUTSET(b)      TFW_CM_REG((b) + 0x004u)
#define TFW_NRF_GPIO_OUTCLR(b)      TFW_CM_REG((b) + 0x008u)
#define TFW_NRF_GPIO_IN(b)          TFW_CM_REG((b) + 0x00Cu)
#define TFW_NRF_GPIO_OUT(b)         TFW_CM_REG((b) + 0x000u)
#define TFW_NRF_GPIO_PIN_CNF(b, n)  TFW_CM_REG((b) + 0x080u + 4u * (uint32_t)(n))

#define TFW_NRF_PIN_CNF_OUTPUT      0x1u          /* DIR output, input buffer connected */
#define TFW_NRF_PIN_CNF_INPUT       0x0u          /* DIR input, input buffer connected, no pull */
#define TFW_NRF_PIN_CNF_PULLUP      (3u << 2)     /* PULL Pullup */
#define TFW_NRF_PIN_CNF_RESET       0x2u          /* input, input buffer disconnected (GPIO_PIN_CNF_ResetValue) */

/* ---- PWM20 (nrf54l15_global.h NRF_PWM20_S_BASE; nrf54l15_types.h,
 * NRF_PWM_Type and its PWM_* fields) */
#define TFW_NRF_PWM_BASE            0x500D2000u
#define TFW_NRF_PWM_CHANNELS        4u            /* PSEL.OUT[4], EVENTS_COMPAREMATCH[4] */
#define TFW_NRF_PWM_STOP            TFW_CM_REG(TFW_NRF_PWM_BASE + 0x004u)   /* TASKS_STOP */
#define TFW_NRF_PWM_SEQ0_START      TFW_CM_REG(TFW_NRF_PWM_BASE + 0x010u)   /* TASKS_DMA.SEQ[0].START */
#define TFW_NRF_PWM_SHORTS          TFW_CM_REG(TFW_NRF_PWM_BASE + 0x200u)
#define TFW_NRF_PWM_INTEN           TFW_CM_REG(TFW_NRF_PWM_BASE + 0x300u)
#define TFW_NRF_PWM_ENABLE          TFW_CM_REG(TFW_NRF_PWM_BASE + 0x500u)
#define TFW_NRF_PWM_MODE            TFW_CM_REG(TFW_NRF_PWM_BASE + 0x504u)
#define TFW_NRF_PWM_COUNTERTOP      TFW_CM_REG(TFW_NRF_PWM_BASE + 0x508u)
#define TFW_NRF_PWM_PRESCALER       TFW_CM_REG(TFW_NRF_PWM_BASE + 0x50Cu)
#define TFW_NRF_PWM_DECODER         TFW_CM_REG(TFW_NRF_PWM_BASE + 0x510u)
#define TFW_NRF_PWM_LOOP            TFW_CM_REG(TFW_NRF_PWM_BASE + 0x514u)
#define TFW_NRF_PWM_SEQ0_REFRESH    TFW_CM_REG(TFW_NRF_PWM_BASE + 0x528u)   /* SEQ[0].REFRESH */
#define TFW_NRF_PWM_SEQ0_ENDDELAY   TFW_CM_REG(TFW_NRF_PWM_BASE + 0x52Cu)   /* SEQ[0].ENDDELAY */
#define TFW_NRF_PWM_PSEL_OUT(n)     TFW_CM_REG(TFW_NRF_PWM_BASE + 0x560u + 4u * (uint32_t)(n))
#define TFW_NRF_PWM_SEQ0_PTR        TFW_CM_REG(TFW_NRF_PWM_BASE + 0x704u)   /* DMA.SEQ[0].PTR */
#define TFW_NRF_PWM_SEQ0_MAXCNT     TFW_CM_REG(TFW_NRF_PWM_BASE + 0x708u)   /* DMA.SEQ[0].MAXCNT, in bytes */

#define TFW_NRF_PWM_ENABLE_ON       1u            /* PWM_ENABLE_ENABLE_Enabled */
#define TFW_NRF_PWM_MODE_UP         0u            /* PWM_MODE_UPDOWN_Up: edge-aligned */
#define TFW_NRF_PWM_PRESCALER_DIV16 4u            /* PWM_PRESCALER_PRESCALER_DIV_16: 1 MHz */
#define TFW_NRF_PWM_DECODER_INDIV   2u            /* PWM_DECODER_LOAD_Individual, MODE_RefreshCount (0) */
#define TFW_NRF_PWM_COUNTERTOP_MAX  0x7FFFu       /* PWM_COUNTERTOP_COUNTERTOP_Msk */
/* A compare value's bit 15 is its polarity, set for a pin high for the first
 * value-many counts of the period: Zephyr's drivers/pwm/pwm_nrfx.c,
 * PWM_NRFX_CH_POLARITY_MASK, for a channel not inverted. Not an MDK field
 * (the sequence is RAM, not a register). */
#define TFW_NRF_PWM_VALUE_HIGH_FIRST 0x8000u

/* ---- WDT30 */
#define TFW_NRF_WDT_BASE            0x50108000u
#define TFW_NRF_WDT_START           TFW_CM_REG(TFW_NRF_WDT_BASE + 0x000u)   /* TASKS_START */
#define TFW_NRF_WDT_STOP            TFW_CM_REG(TFW_NRF_WDT_BASE + 0x004u)   /* TASKS_STOP */
#define TFW_NRF_WDT_EV_TIMEOUT      TFW_CM_REG(TFW_NRF_WDT_BASE + 0x100u)
#define TFW_NRF_WDT_EV_STOPPED      TFW_CM_REG(TFW_NRF_WDT_BASE + 0x104u)
#define TFW_NRF_WDT_INTEN           TFW_CM_REG(TFW_NRF_WDT_BASE + 0x300u)
#define TFW_NRF_WDT_NMIEN           TFW_CM_REG(TFW_NRF_WDT_BASE + 0x320u)
#define TFW_NRF_WDT_RUNSTATUS       TFW_CM_REG(TFW_NRF_WDT_BASE + 0x400u)
#define TFW_NRF_WDT_CRV             TFW_CM_REG(TFW_NRF_WDT_BASE + 0x504u)
#define TFW_NRF_WDT_RREN            TFW_CM_REG(TFW_NRF_WDT_BASE + 0x508u)
#define TFW_NRF_WDT_CONFIG          TFW_CM_REG(TFW_NRF_WDT_BASE + 0x50Cu)
#define TFW_NRF_WDT_TSEN            TFW_CM_REG(TFW_NRF_WDT_BASE + 0x520u)
#define TFW_NRF_WDT_RR0             TFW_CM_REG(TFW_NRF_WDT_BASE + 0x600u)

#define TFW_NRF_WDT_KEY             0x6E524635uL  /* RR reload value and TSEN's stop enable */
#define TFW_NRF_WDT_CONFIG_SLEEP    (1uL << 0)    /* keep counting while the CPU sleeps */
#define TFW_NRF_WDT_CONFIG_STOPEN   (1uL << 6)    /* TASKS_STOP may stop it (after TSEN) */
#define TFW_NRF_WDT_RREN_RR0        1uL
#define TFW_NRF_WDT_LFCLK_HZ        32768uL       /* it counts the 32.768 kHz clock */

/* ---- OSCILLATORS, CLOCK, RESET */
#define TFW_NRF_PLL_FREQ            TFW_CM_REG(0x50120800u)   /* OSCILLATORS.PLL.FREQ */
#define TFW_NRF_PLL_FREQ_CK128M     1u
#define TFW_NRF_CLOCK_XOSTART       TFW_CM_REG(0x5010E000u)   /* CLOCK.TASKS_XOSTART */
#define TFW_NRF_RESETREAS           TFW_CM_REG(0x5010E600u)   /* RESET.RESETREAS, write 1 to clear */
#define TFW_NRF_RESETREAS_RESETPIN  (1uL << 0)

/* ---- FICR */
#define TFW_NRF_FICR_TRIMCNF        0x00FFC400u   /* 64 { ADDR, DATA } pairs */
#define TFW_NRF_FICR_TRIMCNF_COUNT  64u
#define TFW_NRF_FICR_PART           TFW_CM_REG(0x00FFC340u)   /* the errata predicates' var1 */
#define TFW_NRF_FICR_REVISION       TFW_CM_REG(0x00FFC344u)   /* and var2 */
#define TFW_NRF_FICR_ERRATA32_TRIM  TFW_CM_REG(0x00FFC334u)

/* ---- RRAMC (nrf54l15_types.h, NRF_RRAMC_Type; the sequence is
 * haly/nrfy_rramc.h's) */
#define TFW_NRF_RRAMC_BASE          0x5004B000u
#define TFW_NRF_RRAMC_READY         TFW_CM_REG(TFW_NRF_RRAMC_BASE + 0x400u)   /* bit 0: ready */
#define TFW_NRF_RRAMC_READYNEXT     TFW_CM_REG(TFW_NRF_RRAMC_BASE + 0x404u)   /* bit 0: takes the next write */
#define TFW_NRF_RRAMC_CONFIG        TFW_CM_REG(TFW_NRF_RRAMC_BASE + 0x500u)

#define TFW_NRF_RRAMC_READY_BIT     1uL
#define TFW_NRF_RRAMC_CONFIG_WEN    (1uL << 0)    /* write enable; WRITEBUFSIZE (bits 8-13) 0: unbuffered */

#endif /* TFW_CM_NRF54L_REGS_H */
