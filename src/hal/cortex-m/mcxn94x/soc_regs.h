/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x peripherals this port drives, at their Secure aliases (core 0
 * leaves reset Secure with the SAU off, so the 0x5xxxxxxx aliases are the ones
 * it may use). Every address, offset and bit is the SDK's device header
 * MCXN947_cm33_core0.h at the mcux-sdk commit docs/reference/README.md pins,
 * which names the MCXNx4x Reference Manual as its source; the manual's
 * sections are to confirm (docs/boards/frdm-mcxn947.md).
 *
 *   SYSCON0       clock gates, peripheral resets, clock selects and dividers,
 *                 the RAM ECC control
 *   SCG0          the system clock generator: the main clock's source
 *   WWDT0         the windowed watchdog, clocked from the 1 MHz FRO
 *   LP_FLEXCOMM4  the host link, as LPUART4; interrupt 39
 *   GPIO0..4      the RGPIO banks
 *   PORT0..4      their pin controllers: the pin function and the input buffer
 *   CTIMER0..4    the standard timers, PWM (hal_pwm.c, ctimer_pwm.c)
 */
#ifndef TFW_CM_MCXN94X_REGS_H
#define TFW_CM_MCXN94X_REGS_H

#include "cm_regs.h"

/* ---- SYSCON0 */
#define TFW_MCXN_SYSCON_BASE        0x50000000u
#define TFW_MCXN_PRESETCTRLCLR(n)   TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x140u + 4u * (n))
#define TFW_MCXN_AHBCLKCTRLSET(n)   TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x220u + 4u * (n))
#define TFW_MCXN_FCCLKSEL(n)        TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x2B0u + 4u * (n))
#define TFW_MCXN_AHBCLKDIV          TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x380u)
#define TFW_MCXN_WDT0CLKDIV         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x38Cu)
#define TFW_MCXN_FLEXCOMMCLKDIV(n)  TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x850u + 4u * (n))
#define TFW_MCXN_CLOCK_CTRL         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0xA18u)
#define TFW_MCXN_ECC_ENABLE_CTRL    TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0xE44u)

/* AHBCLKCTRL0 / PRESETCTRL0: the pin controllers and the GPIO banks, bank n
 * at bit 13 + n and 19 + n; the watchdog's gate (it has no reset bit). */
#define TFW_MCXN_CTRL0_PORT(n)      (1u << (13u + (n)))
#define TFW_MCXN_CTRL0_GPIO(n)      (1u << (19u + (n)))
#define TFW_MCXN_CTRL0_WWDT0        (1u << 28)
/* AHBCLKCTRL1 / PRESETCTRL1: LP_FLEXCOMM n at bit 11 + n. */
#define TFW_MCXN_CTRL1_FC(n)        (1u << (11u + (n)))
/* AHBCLKCTRL2: the SCG's register interface. */
#define TFW_MCXN_CTRL2_SCG          (1u << 26)

/* A divider register: the count in the low bits, divide by DIV + 1. The SDK
 * halts and resets one before it writes the count (CLOCK_SetClkDiv). */
#define TFW_MCXN_CLKDIV_RESET       (1u << 29)
#define TFW_MCXN_CLKDIV_HALT        (1u << 30)
#define TFW_MCXN_CLKDIV_UNSTAB      (1u << 31)

#define TFW_MCXN_FCCLKSEL_FRO12M    2u

#define TFW_MCXN_CLOCK_CTRL_FRO1MHZ_ENA     (1u << 2)
#define TFW_MCXN_CLOCK_CTRL_FRO1MHZ_CLK_ENA (1u << 6)

#define TFW_MCXN_PORT_COUNT         5u

/* ADC0's gate and reset in AHBCLKCTRL1 / PRESETCTRL1, its clock select and
 * divider. The line numbers here and in the ADC0 block below are of
 * devices/MCXN947/MCXN947_cm33_core0.h at mcux-sdk 8a289764, pinned in
 * docs/reference/README.md. */
#define TFW_MCXN_CTRL1_ADC0         (1u << 3)  /* SYSCON_AHBCLKCTRL1_ADC0_MASK 73684, SYSCON_PRESETCTRL1_ADC0_RST_MASK 72868 */
#define TFW_MCXN_ADC0CLKSEL         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x2A4u)  /* 72242 */
#define TFW_MCXN_ADC0CLKDIV         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x394u)  /* 72263; RESET, HALT 74767, 74775 */
#define TFW_MCXN_ADCCLKSEL_FRO12M   3u         /* SYSCON_ADC0CLKSEL_SEL 74411: FRO 12 MHz */

/* ---- ADC0 (LPADC): ADC_Type, 675-705. CMD[0] is command 1; FIFO 0 of two. */
#define TFW_MCXN_ADC0_BASE          0x5010D000u                                  /* ADC0_BASE 1610 */
#define TFW_MCXN_ADC_CTRL           TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x010u)      /* 675 */
#define TFW_MCXN_ADC_STAT           TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x014u)      /* 676 */
#define TFW_MCXN_ADC_CFG            TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x020u)      /* 679 */
#define TFW_MCXN_ADC_SWTRIG         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x034u)      /* 682 */
#define TFW_MCXN_ADC_TCTRL0         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0A0u)      /* 687 */
#define TFW_MCXN_ADC_GCC(n)         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0F0u + 4u * (n))  /* 691 */
#define TFW_MCXN_ADC_GCR(n)         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0F8u + 4u * (n))  /* 692 */
#define TFW_MCXN_ADC_CMDL1          TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x100u)      /* 694 */
#define TFW_MCXN_ADC_CMDH1          TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x104u)      /* 695 */
#define TFW_MCXN_ADC_RESFIFO0       TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x300u)      /* 700 */

/* Each field's _MASK line in the same header. */
#define TFW_MCXN_ADC_CTRL_ADCEN     (1u << 0)  /* ADC_CTRL_ADCEN 841 */
#define TFW_MCXN_ADC_CTRL_RST       (1u << 1)  /* ADC_CTRL_RST 849 */
#define TFW_MCXN_ADC_CTRL_DOZEN     (1u << 2)  /* ADC_CTRL_DOZEN 857 */
#define TFW_MCXN_ADC_CTRL_CAL_REQ   (1u << 3)  /* ADC_CTRL_CAL_REQ 865 */
#define TFW_MCXN_ADC_CTRL_CALOFS    (1u << 4)  /* ADC_CTRL_CALOFS 873 */
#define TFW_MCXN_ADC_CTRL_RSTFIFO0  (1u << 8)  /* ADC_CTRL_RSTFIFO0 881 */
#define TFW_MCXN_ADC_CTRL_RSTFIFO1  (1u << 9)  /* ADC_CTRL_RSTFIFO1 889 */
#define TFW_MCXN_ADC_CTRL_CAL_AVGS_SHIFT 16u   /* ADC_CTRL_CAL_AVGS 897; 7: 128 averaged, 907 */
#define TFW_MCXN_ADC_STAT_CAL_RDY   (1u << 10) /* ADC_STAT_CAL_RDY 966 */
#define TFW_MCXN_ADC_CFG_REFSEL_SHIFT 6u       /* ADC_CFG_REFSEL 1100; 0: "Option 1", 1103 */
#define TFW_MCXN_ADC_CFG_PUDLY_SHIFT  16u      /* ADC_CFG_PUDLY 1134 */
#define TFW_MCXN_ADC_CFG_PWREN      (1u << 28) /* ADC_CFG_PWREN 1139 */
#define TFW_MCXN_ADC_SWTRIG_SWT0    (1u << 0)  /* ADC_SWTRIG_SWT0 1171 */
#define TFW_MCXN_ADC_TCTRL_TCMD_SHIFT 24u      /* ADC_TCTRL_TCMD 1293 */
#define TFW_MCXN_ADC_GCC_GAIN_CAL   0xFFFFu    /* ADC_GCC_GAIN_CAL 1327 */
#define TFW_MCXN_ADC_GCC_RDY        (1u << 24) /* ADC_GCC_RDY 1332 */
#define TFW_MCXN_ADC_GCR_RDY        (1u << 24) /* ADC_GCR_RDY 1352 */
#define TFW_MCXN_ADC_CMDL_ADCH_MASK 0x1Fu      /* ADC_CMDL_ADCH 1367; CTYPE 0 (A side), MODE 0 (12-bit), 1383, 1393 */
#define TFW_MCXN_ADC_CMDL_CTYPE_B   (1u << 5)  /* ADC_CMDL_CTYPE 1380 (bits 6:5); 1: B side only, 1384 */
#define TFW_MCXN_ADC_CMDH_STS_SHIFT 8u         /* ADC_CMDH_STS 1452; 7: 131.5 ADCK cycles, 1462 */
#define TFW_MCXN_ADC_RESFIFO_D      0xFFFFu    /* ADC_RESFIFO_D 1529 */
#define TFW_MCXN_ADC_RESFIFO_VALID  (1u << 31) /* ADC_RESFIFO_VALID 1565 */

/* The CTIMERs' clock select (FRO12M is 4, fsl_clock.h kFRO12M_to_CTIMERn),
 * divider and function-clock enable, and their gates and resets: CTIMER0, 1,
 * 2 at bits 26, 27, 22 of AHBCLKCTRL1 / PRESETCTRL1, CTIMER3 and 4 at bits 21
 * and 22 of AHBCLKCTRL2 / PRESETCTRL2. */
#define TFW_MCXN_CTIMERCLKSEL(n)    TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x26Cu + 4u * (n))
#define TFW_MCXN_CTIMERCLKDIV(n)    TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x3D0u + 4u * (n))
#define TFW_MCXN_CTIMERGLOBALSTARTEN TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0xE40u)
#define TFW_MCXN_CTIMERCLKSEL_FRO12M 4u
#define TFW_MCXN_CTIMER_COUNT       5u

/* ---- SCG0 */
#define TFW_MCXN_SCG_BASE           0x50044000u
#define TFW_MCXN_SCG_CSR            TFW_CM_REG(TFW_MCXN_SCG_BASE + 0x010u)
#define TFW_MCXN_SCG_RCCR           TFW_CM_REG(TFW_MCXN_SCG_BASE + 0x014u)
#define TFW_MCXN_SCG_SIRCCSR        TFW_CM_REG(TFW_MCXN_SCG_BASE + 0x200u)

#define TFW_MCXN_SCG_SCS_SHIFT      24u
#define TFW_MCXN_SCG_SCS_MASK       (0xFu << TFW_MCXN_SCG_SCS_SHIFT)
#define TFW_MCXN_SCG_SCS_SIRC       2u     /* FRO12M */
#define TFW_MCXN_SCG_SIRC_PERIPH_EN (1u << 5)

/* ---- WWDT0 */
#define TFW_MCXN_WWDT_BASE          0x50016000u
#define TFW_MCXN_WWDT_MOD           TFW_CM_REG(TFW_MCXN_WWDT_BASE + 0x00u)
#define TFW_MCXN_WWDT_TC            TFW_CM_REG(TFW_MCXN_WWDT_BASE + 0x04u)
#define TFW_MCXN_WWDT_FEED          TFW_CM_REG(TFW_MCXN_WWDT_BASE + 0x08u)

#define TFW_MCXN_WWDT_MOD_WDEN      0x1u
#define TFW_MCXN_WWDT_MOD_WDRESET   0x2u
#define TFW_MCXN_WWDT_TC_MAX        0xFFFFFFu
#define TFW_MCXN_WWDT_FEED_1        0xAAu
#define TFW_MCXN_WWDT_FEED_2        0x55u

/* ---- CMC0: the last reset's source (soc_reset.c) */
#define TFW_MCXN_CMC_BASE           0x50048000u
#define TFW_MCXN_CMC_SRS            TFW_CM_REG(TFW_MCXN_CMC_BASE + 0x80u)

/* ---- LP_FLEXCOMM4 as LPUART4: the LPUART's registers at the unit's base,
 * the unit's function select at 0xFF8. */
#define TFW_MCXN_LINK_FC            4u
#define TFW_MCXN_LINK_IRQ           39u    /* LP_FLEXCOMM0 is 35 */
#define TFW_MCXN_FC4_BASE           0x500B4000u
/* The offsets alone, for tfw-boot's polled UART, which takes the unit's base
 * so its host test can hand it a register model. */
#define TFW_MCXN_LPUART_GLOBAL_OFF  0x008u
#define TFW_MCXN_LPUART_BAUD_OFF    0x010u
#define TFW_MCXN_LPUART_STAT_OFF    0x014u
#define TFW_MCXN_LPUART_CTRL_OFF    0x018u
#define TFW_MCXN_LPUART_DATA_OFF    0x01Cu
#define TFW_MCXN_FC_PSELID_OFF      0xFF8u
#define TFW_MCXN_LPUART_GLOBAL      TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_LPUART_GLOBAL_OFF)
#define TFW_MCXN_LPUART_BAUD        TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_LPUART_BAUD_OFF)
#define TFW_MCXN_LPUART_STAT        TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_LPUART_STAT_OFF)
#define TFW_MCXN_LPUART_CTRL        TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_LPUART_CTRL_OFF)
#define TFW_MCXN_LPUART_DATA        TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_LPUART_DATA_OFF)
#define TFW_MCXN_FC_PSELID          TFW_CM_REG(TFW_MCXN_FC4_BASE + TFW_MCXN_FC_PSELID_OFF)

#define TFW_MCXN_LPUART_GLOBAL_RST  (1u << 1)
#define TFW_MCXN_LPUART_STAT_FE     (1u << 17)
#define TFW_MCXN_LPUART_STAT_NF     (1u << 18)
#define TFW_MCXN_LPUART_STAT_OR     (1u << 19)
#define TFW_MCXN_LPUART_STAT_RDRF   (1u << 21)
#define TFW_MCXN_LPUART_STAT_TC     (1u << 22)
#define TFW_MCXN_LPUART_STAT_TDRE   (1u << 23)
#define TFW_MCXN_LPUART_CTRL_RE     (1u << 18)
#define TFW_MCXN_LPUART_CTRL_TE     (1u << 19)
#define TFW_MCXN_LPUART_CTRL_RIE    (1u << 21)
#define TFW_MCXN_LPUART_CTRL_TIE    (1u << 23)
#define TFW_MCXN_FC_PSELID_UART     1u

/* ---- GPIO0..4 (RGPIO) and PORT0..4. The banks are 0x2000 apart from
 * GPIO0, the pin controllers 0x1000 apart from PORT0. */
#define TFW_MCXN_GPIO_BASE(n)       (0x50096000u + 0x2000u * (n))
#define TFW_MCXN_GPIO_PSOR(n)       TFW_CM_REG(TFW_MCXN_GPIO_BASE(n) + 0x44u)
#define TFW_MCXN_GPIO_PCOR(n)       TFW_CM_REG(TFW_MCXN_GPIO_BASE(n) + 0x48u)
#define TFW_MCXN_GPIO_PDIR(n)       TFW_CM_REG(TFW_MCXN_GPIO_BASE(n) + 0x50u)
#define TFW_MCXN_GPIO_PDDR(n)       TFW_CM_REG(TFW_MCXN_GPIO_BASE(n) + 0x54u)

#define TFW_MCXN_PORT_BASE(n)       (0x50116000u + 0x1000u * (n))
#define TFW_MCXN_PORT_PCR(n, bit)   TFW_CM_REG(TFW_MCXN_PORT_BASE(n) + 0x80u + 4u * (bit))

#define TFW_MCXN_PCR_PS             (1u << 0)    /* pull select: 1 = up */
#define TFW_MCXN_PCR_PE             (1u << 1)    /* pull enable */
#define TFW_MCXN_PCR_MUX_SHIFT      8u
#define TFW_MCXN_PCR_IBE            (1u << 12)   /* input buffer enable */

/* ---- CTIMER0..4, 0x1000 apart from CTIMER0; the registers are ctimer_pwm.h's
 * word offsets from the base. */
#define TFW_MCXN_CTIMER_BASE(n)     (0x5000C000u + 0x1000u * (n))

#endif /* TFW_CM_MCXN94X_REGS_H */
