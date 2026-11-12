/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 M7's I2C: one LPI2C instance as a polled master (lpi2c.h for
 * the registers and their sources). No interrupt: MIER stays zero and the
 * vector table does not name the instance.
 *
 * The System Manager owns the pins and the clock (board_profile.h): the HAL
 * muxes nothing and gates no clock, and the divisor counts
 * TFW_IMX95_LPI2C_CLK_HZ, the root the configuration sets. While
 * TFW_IMX95_LPI2C_BASE is zero (the instance is open, design questions 13 and
 * 17) every call answers TFW_ERR_UNSUPPORTED and touches no register.
 *
 * Status as the MPS2 SBCon's (src/hal/cortex-m/mps2/hal_bus.c): an address or
 * data NACK (MSR.NDF) is TFW_ERR_NACK, a transfer that never completes
 * TFW_ERR_TIMEOUT, lost arbitration (ALF) TFW_ERR_BUSY and a FIFO error
 * (FEF, a command out of order, which this file never sends)
 * TFW_ERR_INTERNAL. Every transfer that reached the bus ends with a STOP; an
 * error flushes both FIFOs first, so no queued command follows it.
 *
 * SPI and 1-Wire are not on this board: src/caps/bus.c is one translation
 * unit, so their entry points are here and answer TFW_ERR_UNSUPPORTED.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "cm_regs.h"
#include "lpi2c.h"

#define REG(base, off) TFW_CM_REG((base) + (off))

/* Polls of MSR before a step is given up. A byte at 100 kHz is 90 us; the
 * bound is many times that at the M7's clock (TO CONFIRM on the board). */
#define LPI2C_SPIN 200000u

static tfw_status_t flag_status(uint32_t msr)
{
    if (msr & TFW_LPI2C_MSR_NDF) {
        return TFW_ERR_NACK;
    }
    if (msr & TFW_LPI2C_MSR_ALF) {
        return TFW_ERR_BUSY;
    }
    return TFW_ERR_INTERNAL;
}

/* Wait for a status bit, or an error flag, or the bound. */
static tfw_status_t wait_msr(uintptr_t base, uint32_t want)
{
    uint32_t n;

    for (n = 0u; n < LPI2C_SPIN; n++) {
        uint32_t msr = REG(base, TFW_LPI2C_MSR);

        if (msr & TFW_LPI2C_MSR_ERR) {
            return flag_status(msr);
        }
        if (msr & want) {
            return TFW_OK;
        }
    }
    return TFW_ERR_TIMEOUT;
}

/* One command into MTDR once the transmit FIFO has drained (TXWATER 0). */
static tfw_status_t push(uintptr_t base, uint32_t cmd)
{
    tfw_status_t st = wait_msr(base, TFW_LPI2C_MSR_TDF);

    if (st == TFW_OK) {
        REG(base, TFW_LPI2C_MTDR) = cmd;
    }
    return st;
}

static tfw_status_t pull(uintptr_t base, uint8_t *out)
{
    uint32_t n;

    for (n = 0u; n < LPI2C_SPIN; n++) {
        uint32_t msr = REG(base, TFW_LPI2C_MSR);

        if (msr & TFW_LPI2C_MSR_ERR) {
            return flag_status(msr);
        }
        if (msr & TFW_LPI2C_MSR_RDF) {
            uint32_t d = REG(base, TFW_LPI2C_MRDR);

            if (!(d & TFW_LPI2C_MRDR_RXEMPTY)) {
                *out = (uint8_t)d;
                return TFW_OK;
            }
        }
    }
    return TFW_ERR_TIMEOUT;
}

/* Drop every queued command and received byte, and clear the flags, which
 * also lets the master take commands again after a NACK. */
static void flush(uintptr_t base)
{
    REG(base, TFW_LPI2C_MCR) = TFW_LPI2C_MCR_MEN | TFW_LPI2C_MCR_RTF | TFW_LPI2C_MCR_RRF;
    REG(base, TFW_LPI2C_MSR) = TFW_LPI2C_MSR_FLAGS;
}

/* The STOP every transfer ends with. A flag the last byte raised (a data NACK
 * shows after the FIFO drained) is the transfer's status and is recovered
 * from as any error is. A STOP that never shows is TFW_ERR_TIMEOUT. */
static tfw_status_t finish(uintptr_t base, tfw_status_t st)
{
    uint32_t n;

    if (st != TFW_OK) {
        flush(base);
    }
    REG(base, TFW_LPI2C_MTDR) = TFW_LPI2C_CMD(TFW_LPI2C_CMD_STOP, 0u);
    for (n = 0u; n < LPI2C_SPIN; n++) {
        uint32_t msr = REG(base, TFW_LPI2C_MSR);

        if (msr & TFW_LPI2C_MSR_SDF) {
            REG(base, TFW_LPI2C_MSR) = TFW_LPI2C_MSR_FLAGS;
            return st;
        }
        if ((msr & TFW_LPI2C_MSR_ERR) && st == TFW_OK) {
            st = flag_status(msr);
            flush(base);
            REG(base, TFW_LPI2C_MTDR) = TFW_LPI2C_CMD(TFW_LPI2C_CMD_STOP, 0u);
        }
    }
    flush(base);
    return TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_imx95_lpi2c_init(uintptr_t base, uint32_t clk_hz, uint32_t bus_hz)
{
    uint32_t     mcfgr1, mccr0;
    tfw_status_t st;

    if (base == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    st = tfw_imx95_lpi2c_clock(clk_hz, bus_hz, &mcfgr1, &mccr0);
    if (st != TFW_OK) {
        return st;
    }
    REG(base, TFW_LPI2C_MCR)    = TFW_LPI2C_MCR_RST;
    REG(base, TFW_LPI2C_MCR)    = 0u;
    REG(base, TFW_LPI2C_MIER)   = 0u;
    REG(base, TFW_LPI2C_MCFGR1) = mcfgr1;
    REG(base, TFW_LPI2C_MCCR0)  = mccr0;
    REG(base, TFW_LPI2C_MFCR)   = 0u;   /* both watermarks 0 */
    REG(base, TFW_LPI2C_MSR)    = TFW_LPI2C_MSR_FLAGS;
    REG(base, TFW_LPI2C_MCR)    = TFW_LPI2C_MCR_MEN;
    return TFW_OK;
}

/* The write, then the read after a repeated START (no STOP between), then
 * the STOP. RECEIVE counts the bytes less one, so rlen of 1 to 255 is one
 * command. */
tfw_status_t tfw_imx95_lpi2c_xfer(uintptr_t base, uint8_t addr7, const uint8_t *wr,
                                  uint8_t wlen, uint8_t *rd, uint8_t rlen)
{
    tfw_status_t st = TFW_OK;
    uint8_t      i;

    if (base == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (wlen == 0u && rlen == 0u) {
        return TFW_OK;
    }
    REG(base, TFW_LPI2C_MSR) = TFW_LPI2C_MSR_FLAGS;
    if (wlen) {
        st = push(base, TFW_LPI2C_CMD(TFW_LPI2C_CMD_START, (uint32_t)addr7 << 1));
        for (i = 0u; st == TFW_OK && i < wlen; i++) {
            st = push(base, TFW_LPI2C_CMD(TFW_LPI2C_CMD_TX, wr[i]));
        }
    }
    if (st == TFW_OK && rlen) {
        st = push(base, TFW_LPI2C_CMD(TFW_LPI2C_CMD_START, ((uint32_t)addr7 << 1) | 1u));
        if (st == TFW_OK) {
            st = push(base, TFW_LPI2C_CMD(TFW_LPI2C_CMD_RX, rlen - 1u));
        }
        for (i = 0u; st == TFW_OK && i < rlen; i++) {
            st = pull(base, &rd[i]);
        }
    }
    if (st == TFW_OK) {
        st = wait_msr(base, TFW_LPI2C_MSR_TDF);
    }
    return finish(base, st);
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    return tfw_imx95_lpi2c_init(TFW_IMX95_LPI2C_BASE, TFW_IMX95_LPI2C_CLK_HZ, hz);
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    return tfw_imx95_lpi2c_xfer(TFW_IMX95_LPI2C_BASE, addr7, wr, wlen, rd, rlen);
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    (void)mode;
    (void)clkdiv;
    (void)lsb_first;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                            uint8_t *rd, uint8_t len)
{
    (void)cs;
    (void)wr;
    (void)rd;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    (void)p;
    (void)out_presence;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    (void)p;
    (void)buf;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p;
    (void)buf;
    (void)len;
    return TFW_ERR_UNSUPPORTED;
}
