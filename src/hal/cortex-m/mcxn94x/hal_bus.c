/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x buses: I2C as a polled LPI2C master on LP_FLEXCOMM2, the unit
 * and pins boards/frdm-mcxn947/board_profile.h names. SPI and 1-Wire are not
 * driven by this port and refuse; they are here because src/caps/bus.c is one
 * translation unit with this file.
 *
 * The bus is built, not run: no board has carried an element on it
 * (docs/boards/frdm-mcxn947.md). The transfer is host-tested against a model
 * of the controller (test/unit/test_mcxn_lpi2c.c).
 *
 * Every register and bit is soc_regs.h's, from the SDK's device header. The
 * sequence is the SDK's LPI2C master (drivers/lpi2c/fsl_lpi2c.c): commands
 * into MTDR (START with the address, transmit, receive n bytes, STOP), the
 * received bytes out of MRDR, and after an error the flags cleared and both
 * FIFOs reset (LPI2C_MasterCheckAndClearError) before the STOP.
 *
 * The status codes are src/hal/cortex-m/mps2/hal_bus.c's: an address or a
 * data byte not acknowledged is TFW_ERR_NACK, a bus that does not come free,
 * or a controller that stops moving, is TFW_ERR_TIMEOUT, and every return
 * after the START was queued has asked for the STOP. A lost arbitration is
 * TFW_ERR_BUSY, a FIFO error TFW_ERR_INTERNAL.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "lpi2c_timing.h"

/* The unit's functional clock: FRO12M, the source hal_uart.c gives the link,
 * divided by 1. */
#define I2C_CLK_HZ   12000000u

/* How long one wait may take: the bus coming free, room in the transmit FIFO,
 * a received byte, the STOP. 10 ms is 100 bytes at 100 kHz, past any clock
 * stretch the element drivers allow for. */
#define I2C_WAIT_US  10000u

/* The controller's registers. A host test defines these first to put its
 * model in place of the unit (test/unit/test_mcxn_lpi2c.c). */
#ifndef TFW_MCXN_I2C_RD
#define TFW_MCXN_I2C_RD(off)    TFW_CM_REG(TFW_MCXN_I2C_BASE + (off))
#define TFW_MCXN_I2C_WR(off, v) (TFW_CM_REG(TFW_MCXN_I2C_BASE + (off)) = (v))
#endif

#define MSR_ERRORS (TFW_MCXN_LPI2C_MSR_NDF | TFW_MCXN_LPI2C_MSR_ALF | \
                    TFW_MCXN_LPI2C_MSR_FEF | TFW_MCXN_LPI2C_MSR_PLTF)
#define MSR_W1C    (TFW_MCXN_LPI2C_MSR_EPF | TFW_MCXN_LPI2C_MSR_SDF | MSR_ERRORS)

static tfw_status_t msr_error(uint32_t msr)
{
    if (msr & TFW_MCXN_LPI2C_MSR_NDF) {
        return TFW_ERR_NACK;
    }
    if (msr & TFW_MCXN_LPI2C_MSR_ALF) {
        return TFW_ERR_BUSY;
    }
    if (msr & TFW_MCXN_LPI2C_MSR_PLTF) {
        return TFW_ERR_TIMEOUT;
    }
    if (msr & TFW_MCXN_LPI2C_MSR_FEF) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

/* Waits for room in the transmit FIFO, then queues one command. */
static tfw_status_t push(uint32_t cmd, uint32_t data)
{
    for (uint32_t us = 0u; us < I2C_WAIT_US; us++) {
        uint32_t  msr = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MSR_OFF);
        tfw_status_t st = msr_error(msr);

        if (st != TFW_OK) {
            return st;
        }
        if (msr & TFW_MCXN_LPI2C_MSR_TDF) {
            TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MTDR_OFF,
                            (cmd << TFW_MCXN_LPI2C_MTDR_CMD_SHIFT) | (data & 0xFFu));
            return TFW_OK;
        }
        tfw_hal_delay_us(1u);
    }
    return TFW_ERR_TIMEOUT;
}

/* Waits for one received byte. */
static tfw_status_t pull(uint8_t *out)
{
    for (uint32_t us = 0u; us < I2C_WAIT_US; us++) {
        uint32_t     rdr = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MRDR_OFF);
        tfw_status_t st;

        if (!(rdr & TFW_MCXN_LPI2C_MRDR_RXEMPTY)) {
            *out = (uint8_t)(rdr & TFW_MCXN_LPI2C_MRDR_DATA_MASK);
            return TFW_OK;
        }
        st = msr_error(TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MSR_OFF));
        if (st != TFW_OK) {
            return st;
        }
        tfw_hal_delay_us(1u);
    }
    return TFW_ERR_TIMEOUT;
}

/* Ends whatever the controller still holds: the reset of the master logic
 * drops a transfer it never finished and both FIFOs, and takes the timing
 * with it, so that goes back in before the master is on again. */
static void abort_master(void)
{
    uint32_t cfgr1 = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MCFGR1_OFF);
    uint32_t cfgr2 = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MCFGR2_OFF);
    uint32_t ccr0 = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MCCR0_OFF);

    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_RST);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, 0u);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCFGR1_OFF, cfgr1);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCFGR2_OFF, cfgr2);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCCR0_OFF, ccr0);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MSR_OFF, MSR_W1C);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_MEN);
}

/* Ends a transfer that queued a START. After an error the flags are cleared
 * and the FIFOs emptied first, as the SDK does, so the STOP is the next
 * command the controller sees; a controller that already let the bus go
 * (MBF clear) needs none. Returns st, or TFW_ERR_TIMEOUT if the STOP never
 * showed. A STOP that cannot be queued or never shows leaves a transfer
 * the controller would carry on with, so it is aborted (abort_master). */
static tfw_status_t finish(tfw_status_t st)
{
    uint32_t     msr;
    tfw_status_t ps;

    if (st != TFW_OK) {
        TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_MEN |
                        TFW_MCXN_LPI2C_MCR_RTF | TFW_MCXN_LPI2C_MCR_RRF);
        TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MSR_OFF, MSR_W1C);
        if (!(TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MSR_OFF) & TFW_MCXN_LPI2C_MSR_MBF)) {
            return st;
        }
    }
    ps = push(TFW_MCXN_LPI2C_CMD_STOP, 0u);
    if (ps != TFW_OK) {
        /* A flag the last byte raised: clear it and stop, as for any error. */
        if (st == TFW_OK && ps != TFW_ERR_TIMEOUT) {
            return finish(ps);
        }
        abort_master();
        return (st != TFW_OK) ? st : TFW_ERR_TIMEOUT;
    }
    for (uint32_t us = 0u; us < I2C_WAIT_US; us++) {
        msr = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MSR_OFF);
        if (msr & TFW_MCXN_LPI2C_MSR_SDF) {
            TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MSR_OFF, MSR_W1C);
            if (st == TFW_OK) {
                st = msr_error(msr);
            }
            return st;
        }
        /* A refusal of the last byte queued shows only now: the controller
         * holds its FIFO until the flag is cleared, so end it as any error. */
        if (st == TFW_OK && (msr & MSR_ERRORS)) {
            return finish(msr_error(msr));
        }
        tfw_hal_delay_us(1u);
    }
    abort_master();
    return (st != TFW_OK) ? st : TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    tfw_mcxn_lpi2c_timing_t t;
    uint32_t pcr = (TFW_MCXN_I2C_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT) | TFW_MCXN_PCR_PS |
                   TFW_MCXN_PCR_PE | TFW_MCXN_PCR_ODE | TFW_MCXN_PCR_IBE;

    if (tfw_mcxn_lpi2c_timing(I2C_CLK_HZ, hz, &t) != TFW_OK) {
        return TFW_ERR_RANGE;
    }
    TFW_MCXN_AHBCLKCTRLSET(0) = TFW_MCXN_CTRL0_PORT(TFW_MCXN_I2C_PORT);
    TFW_MCXN_PRESETCTRLCLR(0) = TFW_MCXN_CTRL0_PORT(TFW_MCXN_I2C_PORT);
    TFW_MCXN_AHBCLKCTRLSET(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_I2C_FC);
    TFW_MCXN_PRESETCTRLCLR(1) = TFW_MCXN_CTRL1_FC(TFW_MCXN_I2C_FC);
    TFW_MCXN_FCCLKSEL(TFW_MCXN_I2C_FC) = TFW_MCXN_FCCLKSEL_FRO12M;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_I2C_FC) = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_FLEXCOMMCLKDIV(TFW_MCXN_I2C_FC) = 0u;
    TFW_MCXN_FC2_PSELID = TFW_MCXN_FC_PSELID_I2C;

    /* Open drain with the pull-up on, input buffer on: the SDK's LPI2C pin
     * configuration for these pins. */
    TFW_MCXN_PORT_PCR(TFW_MCXN_I2C_PORT, TFW_MCXN_I2C_SDA_BIT) = pcr;
    TFW_MCXN_PORT_PCR(TFW_MCXN_I2C_PORT, TFW_MCXN_I2C_SCL_BIT) = pcr;

    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_RST);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, 0u);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCFGR1_OFF, t.mcfgr1);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCFGR2_OFF, t.mcfgr2);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCCR0_OFF, t.mccr0);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_MEN);
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    tfw_status_t st = TFW_OK;
    uint32_t     us;
    uint8_t      i;

    if (wlen == 0u && rlen == 0u) {
        return TFW_OK;
    }
    /* A clean start: no flag from the last transfer, nothing left queued. */
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MSR_OFF, MSR_W1C);
    TFW_MCXN_I2C_WR(TFW_MCXN_LPI2C_MCR_OFF, TFW_MCXN_LPI2C_MCR_MEN |
                    TFW_MCXN_LPI2C_MCR_RTF | TFW_MCXN_LPI2C_MCR_RRF);
    /* The bus busy is another master, a stuck line, or this master still in
     * a transfer it never ended: wait for it to go idle, then give up
     * without touching it. A START on a bus this master holds would be a
     * repeated START of the old transfer. */
    for (us = 0u; us < I2C_WAIT_US; us++) {
        uint32_t msr = TFW_MCXN_I2C_RD(TFW_MCXN_LPI2C_MSR_OFF);

        if (!(msr & (TFW_MCXN_LPI2C_MSR_BBF | TFW_MCXN_LPI2C_MSR_MBF))) {
            break;
        }
        tfw_hal_delay_us(1u);
    }
    if (us == I2C_WAIT_US) {
        return TFW_ERR_TIMEOUT;
    }

    if (wlen) {
        st = push(TFW_MCXN_LPI2C_CMD_START, (uint32_t)addr7 << 1);
        for (i = 0u; st == TFW_OK && i < wlen; i++) {
            st = push(TFW_MCXN_LPI2C_CMD_TX, wr[i]);
        }
    }
    if (st == TFW_OK && rlen) {
        st = push(TFW_MCXN_LPI2C_CMD_START, ((uint32_t)addr7 << 1) | 1u);
        if (st == TFW_OK) {
            st = push(TFW_MCXN_LPI2C_CMD_RX, (uint32_t)rlen - 1u);
        }
        for (i = 0u; st == TFW_OK && i < rlen; i++) {
            st = pull(&rd[i]);
        }
    }
    return finish(st);
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
