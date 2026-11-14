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
 *   LP_FLEXCOMM2  the I2C master, as LPI2C2, polled
 *   GPIO0..4      the RGPIO banks
 *   PORT0..4      their pin controllers: the pin function and the input buffer
 *   CTIMER0..4    the standard timers, PWM (hal_pwm.c, ctimer_pwm.c)
 *   CTIMER2       the tone's timer (hal_tone.c)
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
#define TFW_MCXN_CTRL1_ADC0         (1u << 3)  /* SYSCON_AHBCLKCTRL1_ADC0_MASK 73685, SYSCON_PRESETCTRL1_ADC0_RST_MASK 72869 */
#define TFW_MCXN_ADC0CLKSEL         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x2A4u)  /* 72243 */
#define TFW_MCXN_ADC0CLKDIV         TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x394u)  /* 72264; RESET, HALT 74768, 74776 */
#define TFW_MCXN_ADCCLKSEL_FRO12M   3u         /* SYSCON_ADC0CLKSEL_SEL 74412: FRO 12 MHz */

/* ---- ADC0 (LPADC): ADC_Type, 672-706. CMD[0] is command 1; FIFO 0 of two. */
#define TFW_MCXN_ADC0_BASE          0x5010D000u                                  /* ADC0_BASE 1611 */
#define TFW_MCXN_ADC_CTRL           TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x010u)      /* 676 */
#define TFW_MCXN_ADC_STAT           TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x014u)      /* 677 */
#define TFW_MCXN_ADC_CFG            TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x020u)      /* 680 */
#define TFW_MCXN_ADC_SWTRIG         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x034u)      /* 683 */
#define TFW_MCXN_ADC_TCTRL0         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0A0u)      /* 688 */
#define TFW_MCXN_ADC_GCC(n)         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0F0u + 4u * (n))  /* 692 */
#define TFW_MCXN_ADC_GCR(n)         TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x0F8u + 4u * (n))  /* 693 */
#define TFW_MCXN_ADC_CMDL1          TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x100u)      /* 695 */
#define TFW_MCXN_ADC_CMDH1          TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x104u)      /* 696 */
#define TFW_MCXN_ADC_RESFIFO0       TFW_CM_REG(TFW_MCXN_ADC0_BASE + 0x300u)      /* 701 */

/* Each field's _MASK line in the same header. */
#define TFW_MCXN_ADC_CTRL_ADCEN     (1u << 0)  /* ADC_CTRL_ADCEN 842 */
#define TFW_MCXN_ADC_CTRL_RST       (1u << 1)  /* ADC_CTRL_RST 850 */
#define TFW_MCXN_ADC_CTRL_DOZEN     (1u << 2)  /* ADC_CTRL_DOZEN 858 */
#define TFW_MCXN_ADC_CTRL_CAL_REQ   (1u << 3)  /* ADC_CTRL_CAL_REQ 866 */
#define TFW_MCXN_ADC_CTRL_CALOFS    (1u << 4)  /* ADC_CTRL_CALOFS 874 */
#define TFW_MCXN_ADC_CTRL_RSTFIFO0  (1u << 8)  /* ADC_CTRL_RSTFIFO0 882 */
#define TFW_MCXN_ADC_CTRL_RSTFIFO1  (1u << 9)  /* ADC_CTRL_RSTFIFO1 890 */
#define TFW_MCXN_ADC_CTRL_CAL_AVGS_SHIFT 16u   /* ADC_CTRL_CAL_AVGS 898; 7: 128 averaged, 908 */
#define TFW_MCXN_ADC_STAT_CAL_RDY   (1u << 10) /* ADC_STAT_CAL_RDY 967 */
#define TFW_MCXN_ADC_CFG_REFSEL_SHIFT 6u       /* ADC_CFG_REFSEL 1101; 0: "Option 1", 1104 */
#define TFW_MCXN_ADC_CFG_PUDLY_SHIFT  16u      /* ADC_CFG_PUDLY 1135 */
#define TFW_MCXN_ADC_CFG_PWREN      (1u << 28) /* ADC_CFG_PWREN 1140 */
#define TFW_MCXN_ADC_SWTRIG_SWT0    (1u << 0)  /* ADC_SWTRIG_SWT0 1172 */
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

/* ---- LP_FLEXCOMM2 as LPI2C2, the I2C master (hal_bus.c). The unit's base is
 * LP_FLEXCOMM2_BASE; its LPI2C registers sit at LPI2C2_BASE, 0x800 into the
 * unit, and PSELID at the same 0xFF8 as every unit's. Offsets are the
 * LPI2C_Type members' (MCXN947_cm33_core0.h); bits the LPI2C_*_MASK values. */
#define TFW_MCXN_I2C_FC             2u
#define TFW_MCXN_FC2_BASE           0x50094000u   /* LP_FLEXCOMM2_BASE */
#define TFW_MCXN_I2C_BASE           0x50094800u   /* LPI2C2_BASE */
#define TFW_MCXN_FC2_PSELID         TFW_CM_REG(TFW_MCXN_FC2_BASE + TFW_MCXN_FC_PSELID_OFF)
#define TFW_MCXN_FC_PSELID_I2C      3u            /* PERSEL 0b011 */

#define TFW_MCXN_LPI2C_MCR_OFF      0x010u
#define TFW_MCXN_LPI2C_MSR_OFF      0x014u
#define TFW_MCXN_LPI2C_MCFGR1_OFF   0x024u
#define TFW_MCXN_LPI2C_MCFGR2_OFF   0x028u
#define TFW_MCXN_LPI2C_MCCR0_OFF    0x048u
#define TFW_MCXN_LPI2C_MFSR_OFF     0x05Cu
#define TFW_MCXN_LPI2C_MTDR_OFF     0x060u
#define TFW_MCXN_LPI2C_MRDR_OFF     0x070u

#define TFW_MCXN_LPI2C_MCR_MEN      (1u << 0)
#define TFW_MCXN_LPI2C_MCR_RST      (1u << 1)
#define TFW_MCXN_LPI2C_MCR_RTF      (1u << 8)
#define TFW_MCXN_LPI2C_MCR_RRF      (1u << 9)

#define TFW_MCXN_LPI2C_MSR_TDF      (1u << 0)
#define TFW_MCXN_LPI2C_MSR_RDF      (1u << 1)
#define TFW_MCXN_LPI2C_MSR_EPF      (1u << 8)
#define TFW_MCXN_LPI2C_MSR_SDF      (1u << 9)
#define TFW_MCXN_LPI2C_MSR_NDF      (1u << 10)
#define TFW_MCXN_LPI2C_MSR_ALF      (1u << 11)
#define TFW_MCXN_LPI2C_MSR_FEF      (1u << 12)
#define TFW_MCXN_LPI2C_MSR_PLTF     (1u << 13)
#define TFW_MCXN_LPI2C_MSR_MBF      (1u << 24)
#define TFW_MCXN_LPI2C_MSR_BBF      (1u << 25)

/* MCFGR1's PRESCALE, MCFGR2's FILTSDA and MCCR0's four fields are
 * lpi2c_timing.h's, which computes them without this header. */

#define TFW_MCXN_LPI2C_MTDR_CMD_SHIFT       8u
#define TFW_MCXN_LPI2C_CMD_TX               0u    /* transmit DATA */
#define TFW_MCXN_LPI2C_CMD_RX               1u    /* receive DATA + 1 bytes */
#define TFW_MCXN_LPI2C_CMD_STOP             2u
#define TFW_MCXN_LPI2C_CMD_START            4u    /* (repeated) START, address */

#define TFW_MCXN_LPI2C_MRDR_DATA_MASK       0xFFu
#define TFW_MCXN_LPI2C_MRDR_RXEMPTY         (1u << 14)

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
#define TFW_MCXN_PCR_ODE            (1u << 5)    /* open drain enable */
#define TFW_MCXN_PCR_IBE            (1u << 12)   /* input buffer enable */

/* ---- CTIMER0..4, 0x1000 apart from CTIMER0; the registers are ctimer_pwm.h's
 * word offsets from the base. */
#define TFW_MCXN_CTIMER_BASE(n)     (0x5000C000u + 0x1000u * (n))

/* ---- CTIMER2, the tone (hal_tone.c): timer mode, match 1 resetting the
 * counter and toggling its external output CT2_MAT1. CTIMER2_BASE; the
 * registers are CTIMER_Type's, as offsets from the base so the host test can
 * name them. SYSCON0's CTIMERCLKSEL[2], CTIMERCLKDIV[2] and
 * CTIMERGLOBALSTARTEN, and the timer's gate and reset at bit 22 of
 * AHBCLKCTRL1 / PRESETCTRL1 (SYSCON_AHBCLKCTRL1_TIMER2,
 * SYSCON_PRESETCTRL1_TIMER2_RST). */
#define TFW_MCXN_TONE_CT            2u
#define TFW_MCXN_CT2_BASE           0x5000E000u
#define TFW_MCXN_CT_TCR_OFF         0x04u
#define TFW_MCXN_CT_PR_OFF          0x0Cu
#define TFW_MCXN_CT_MCR_OFF         0x14u
#define TFW_MCXN_CT_MR_OFF(n)       (0x18u + 4u * (n))
#define TFW_MCXN_CT_EMR_OFF         0x3Cu
#define TFW_MCXN_CT2_TCR            TFW_CM_REG(TFW_MCXN_CT2_BASE + TFW_MCXN_CT_TCR_OFF)
#define TFW_MCXN_CT2_PR             TFW_CM_REG(TFW_MCXN_CT2_BASE + TFW_MCXN_CT_PR_OFF)
#define TFW_MCXN_CT2_MCR            TFW_CM_REG(TFW_MCXN_CT2_BASE + TFW_MCXN_CT_MCR_OFF)
#define TFW_MCXN_CT2_MR1            TFW_CM_REG(TFW_MCXN_CT2_BASE + TFW_MCXN_CT_MR_OFF(1u))
#define TFW_MCXN_CT2_EMR            TFW_CM_REG(TFW_MCXN_CT2_BASE + TFW_MCXN_CT_EMR_OFF)

#define TFW_MCXN_CT_TCR_CEN         (1u << 0)    /* counter enable */
#define TFW_MCXN_CT_TCR_CRST        (1u << 1)    /* counter reset, held while set */
#define TFW_MCXN_CT_MCR_MR1R        (1u << 4)    /* reset the counter on match 1 */
#define TFW_MCXN_CT_EMR_EM1         (1u << 1)    /* match 1's output state */
#define TFW_MCXN_CT_EMR_EMC1_SHIFT  6u
#define TFW_MCXN_CT_EMR_EMC_TOGGLE  3u           /* 0b11 "Toggles" */

#define TFW_MCXN_CT2CLKSEL          TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x26Cu + 4u * TFW_MCXN_TONE_CT)
#define TFW_MCXN_CT2CLKDIV          TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0x3D0u + 4u * TFW_MCXN_TONE_CT)
#define TFW_MCXN_CTGLOBALSTARTEN    TFW_CM_REG(TFW_MCXN_SYSCON_BASE + 0xE40u)
#define TFW_MCXN_CTCLKSEL_FRO12M    4u           /* 0b0100 "FRO 12MHz clock" */
#define TFW_MCXN_CTRL1_TIMER2       (1u << 22)
#define TFW_MCXN_CTGLOBAL_CT2       (1u << 2)    /* CTIMER2_CLK_EN */

#endif /* TFW_CM_MCXN94X_REGS_H */
