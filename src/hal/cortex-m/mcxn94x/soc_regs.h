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

#endif /* TFW_CM_MCXN94X_REGS_H */
