/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95's LPI2C master registers, for hal_bus.c only. Kept out of
 * soc_regs.h so the PWM and ADC files do not share an edit with it.
 *
 * Each offset and bit is NXP's MCUXpresso SDK device header (MIMX9596_cm7.h
 * and the periph/PERI_LPI2C.h it includes, nxp-mcuxpresso/mcux-devices-imx
 * commit d0112c674564adbc10418148001d13b5541ed532) and agrees with Linux's
 * driver (drivers/i2c/busses/i2c-imx-lpi2c.c, v6.12) wherever Linux names it;
 * every value was checked against both at those revisions. EPF and
 * FEF are the SDK header's alone. Both are rows of docs/reference/README.md,
 * read for facts only. TO CONFIRM against IMX95RM, which this project does not
 * hold.
 *
 * Which LPI2C instance the System Manager gives the M7 is open
 * (docs/multi-arch/imx95-design.md questions 13 and 17): the base is
 * TFW_IMX95_LPI2C_BASE in board_profile.h, zero (unsupported) until known.
 */
#ifndef TFW_CM_IMX95_LPI2C_H
#define TFW_CM_IMX95_LPI2C_H

#include <stdint.h>

#include "tfw/hal.h"

/* Offsets from the instance's base (SDK LPI2C_Type; Linux LPI2C_M*). */
#define TFW_LPI2C_MCR     0x10u
#define TFW_LPI2C_MSR     0x14u
#define TFW_LPI2C_MIER    0x18u
#define TFW_LPI2C_MCFGR1  0x24u
#define TFW_LPI2C_MCCR0   0x48u
#define TFW_LPI2C_MFCR    0x58u
#define TFW_LPI2C_MTDR    0x60u
#define TFW_LPI2C_MRDR    0x70u

/* MCR (SDK LPI2C_MCR_*; Linux MCR_*). */
#define TFW_LPI2C_MCR_MEN  (1u << 0)    /* master enable */
#define TFW_LPI2C_MCR_RST  (1u << 1)    /* software reset */
#define TFW_LPI2C_MCR_RTF  (1u << 8)    /* reset the transmit FIFO */
#define TFW_LPI2C_MCR_RRF  (1u << 9)    /* reset the receive FIFO */

/* MSR (SDK LPI2C_MSR_*). The five flags are write-1-to-clear. */
#define TFW_LPI2C_MSR_TDF  (1u << 0)    /* transmit FIFO at or under its watermark */
#define TFW_LPI2C_MSR_RDF  (1u << 1)    /* receive FIFO over its watermark */
#define TFW_LPI2C_MSR_EPF  (1u << 8)    /* end of packet: a START or STOP sent */
#define TFW_LPI2C_MSR_SDF  (1u << 9)    /* STOP sent */
#define TFW_LPI2C_MSR_NDF  (1u << 10)   /* NACK: address or data refused */
#define TFW_LPI2C_MSR_ALF  (1u << 11)   /* arbitration lost */
#define TFW_LPI2C_MSR_FEF  (1u << 12)   /* FIFO error: a command out of order */
#define TFW_LPI2C_MSR_FLAGS (TFW_LPI2C_MSR_EPF | TFW_LPI2C_MSR_SDF | TFW_LPI2C_MSR_NDF | \
                             TFW_LPI2C_MSR_ALF | TFW_LPI2C_MSR_FEF)
#define TFW_LPI2C_MSR_ERR  (TFW_LPI2C_MSR_NDF | TFW_LPI2C_MSR_ALF | TFW_LPI2C_MSR_FEF)

/* MCFGR1.PRESCALE, bits 2:0; MCCR0's four 6-bit fields (SDK masks). */
#define TFW_LPI2C_PRESCALE_MAX 7u
#define TFW_LPI2C_MCCR0_VAL(lo, hi, sethold, datavd) \
    (((uint32_t)(lo) & 0x3Fu) | (((uint32_t)(hi) & 0x3Fu) << 8) | \
     (((uint32_t)(sethold) & 0x3Fu) << 16) | (((uint32_t)(datavd) & 0x3Fu) << 24))

/* MTDR: the command in bits 10:8, its data in 7:0 (SDK LPI2C_MTDR_CMD;
 * Linux TRAN_DATA, RECV_DATA, GEN_STOP, GEN_START). RECEIVE takes the count
 * less one, so at most 256 bytes per command. */
#define TFW_LPI2C_CMD_TX     0u
#define TFW_LPI2C_CMD_RX     1u
#define TFW_LPI2C_CMD_STOP   2u
#define TFW_LPI2C_CMD_START  4u
#define TFW_LPI2C_CMD(c, d)  ((uint32_t)(((uint32_t)(c) << 8) | ((uint32_t)(d) & 0xFFu)))

/* MRDR: the byte in 7:0, RXEMPTY in bit 14 (SDK; Linux MRDR_RXEMPTY). */
#define TFW_LPI2C_MRDR_RXEMPTY (1u << 14)

/* MCFGR1 and MCCR0 for a bus rate from a functional clock: Linux's
 * lpi2c_imx_config() arithmetic, the smallest prescaler whose low count fits
 * six bits, high a third of the cycle rounded up, SETHOLD the high count and
 * DATAVD half of it. The glitch filters (MCFGR2) stay at reset, off, so the
 * filter term of Linux's cycle is zero. TFW_ERR_RANGE for a rate the clock
 * cannot make, nothing written. */
static inline tfw_status_t tfw_imx95_lpi2c_clock(uint32_t clk_hz, uint32_t bus_hz,
                                                 uint32_t *mcfgr1, uint32_t *mccr0)
{
    uint32_t pre;

    if (bus_hz == 0u) {
        return TFW_ERR_RANGE;
    }
    for (pre = 0u; pre <= TFW_LPI2C_PRESCALE_MAX; pre++) {
        uint32_t q = (clk_hz >> pre) / bus_hz;   /* clk / (2^pre * bus), no overflow */
        uint32_t cycle, hi, lo;

        if (q <= 4u) {
            return TFW_ERR_RANGE;
        }
        cycle = q - 3u;
        hi    = (cycle + 2u) / 3u;
        lo    = cycle - hi;
        if (lo < 64u) {
            *mcfgr1 = pre;
            *mccr0  = TFW_LPI2C_MCCR0_VAL(lo, hi, hi, hi >> 1);
            return TFW_OK;
        }
    }
    return TFW_ERR_RANGE;
}

/* The bus at an instance's base, which hal_bus.c's tfw_hal_i2c_* call with
 * TFW_IMX95_LPI2C_BASE and the host test with a modelled one. Base zero
 * answers TFW_ERR_UNSUPPORTED and touches no register. */
tfw_status_t tfw_imx95_lpi2c_init(uintptr_t base, uint32_t clk_hz, uint32_t bus_hz);
tfw_status_t tfw_imx95_lpi2c_xfer(uintptr_t base, uint8_t addr7, const uint8_t *wr,
                                  uint8_t wlen, uint8_t *rd, uint8_t rlen);

#endif /* TFW_CM_IMX95_LPI2C_H */
