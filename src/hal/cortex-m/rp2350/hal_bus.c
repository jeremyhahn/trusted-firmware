/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's bus HAL (row rp2350): the I2C master on I2C0, a polled
 * Synopsys DW_apb_i2c, at GPIO4 (SDA) and GPIO5 (SCL), the pins pico-sdk
 * 2.1.1's pico2_w.h names PICO_DEFAULT_I2C. SPI and 1-Wire are refused with
 * TFW_ERR_UNSUPPORTED: src/caps/bus.c is one translation unit and links all
 * three. Registers and bits are soc_regs.h's, each cited to [RP-DS] release 8
 * §12.2; the SCL arithmetic is soc_calc.h's, pinned by test_rp2350_soc.c.
 * test_rp2350_i2c.c runs this file over a register model.
 *
 * The status codes are the MPS2 master's (src/hal/cortex-m/mps2/hal_bus.c):
 *
 *   TFW_ERR_NACK     the address or a data byte was not acknowledged
 *                    (IC_TX_ABRT_SOURCE bits 0-4, table 1138);
 *   TFW_ERR_BUSY     arbitration lost to another master (bit 12);
 *   TFW_ERR_TIMEOUT  the controller did not go idle, a command did not
 *                    complete, or no STOP was seen, within TFW_RP_I2C_POLLS
 *                    reads: a bus held low.
 *
 * Every transaction ends with a STOP on the bus. The last command carries
 * IC_DATA_CMD.STOP (§12.2.5.1). After an abort the controller flushes the
 * FIFOs (§12.2.17 table 1119, TX_ABRT); [RP-DS] does not say in so many
 * words that it then issues a STOP, so the driver waits for STOP_DET and, if
 * none comes, sets IC_ENABLE.ABORT, which does (§12.2.10.4). A timeout does
 * the same, then disables the block, so the next call starts from idle.
 *
 * Not run on a board: built and host-tested only (docs/boards/rp2350-pico2w.md).
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_calc.h"

/* Status reads one wait may take before it gives up. A byte at the slowest
 * rate tfw_hal_i2c_init accepts from a 12 MHz clk_sys (the 16-bit counts)
 * is about 9 * 131072 cycles, and each read is several: this bounds every
 * wait at a few hundred milliseconds. */
#ifndef TFW_RP_I2C_POLLS
#define TFW_RP_I2C_POLLS 400000uL
#endif

static uint8_t wait_disabled(void)
{
    uint32_t n;

    for (n = 0u; n < TFW_RP_I2C_POLLS; n++) {
        if ((TFW_RP_IC_ENABLE_STATUS & TFW_RP_IC_ENABLE_STATUS_EN) == 0u) {
            return 1u;
        }
    }
    return 0u;
}

/* Waits for any bit of mask in IC_RAW_INTR_STAT; returns what it saw, or 0. */
static uint32_t wait_intr(uint32_t mask)
{
    uint32_t n;

    for (n = 0u; n < TFW_RP_I2C_POLLS; n++) {
        uint32_t raw = TFW_RP_IC_RAW_INTR_STAT & mask;

        if (raw != 0u) {
            return raw;
        }
    }
    return 0u;
}

/* Waits until the command just queued is taken (want TFE) or its byte has
 * arrived (want RFNE), or a transmit abort. 1 done, 0 aborted, 2 timed out. */
static uint8_t wait_cmd(uint32_t want)
{
    uint32_t n;

    for (n = 0u; n < TFW_RP_I2C_POLLS; n++) {
        if ((TFW_RP_IC_RAW_INTR_STAT & TFW_RP_IC_INTR_TX_ABRT) != 0u) {
            return 0u;
        }
        if ((TFW_RP_IC_STATUS & want) != 0u) {
            return 1u;
        }
    }
    return 2u;
}

/* The bus did not come free, or a command or the STOP never completed:
 * ABORT (a STOP and a FIFO flush, §12.2.10.4), then disable the block. */
static tfw_status_t abandon(void)
{
    if ((TFW_RP_IC_ENABLE & TFW_RP_IC_ENABLE_ENABLE) != 0u) {
        uint32_t n;

        TFW_RP_IC_ENABLE = TFW_RP_IC_ENABLE_ENABLE | TFW_RP_IC_ENABLE_ABORT;
        for (n = 0u; n < TFW_RP_I2C_POLLS; n++) {
            if ((TFW_RP_IC_ENABLE & TFW_RP_IC_ENABLE_ABORT) == 0u) {
                break;
            }
        }
    }
    (void)TFW_RP_IC_CLR_INTR;
    TFW_RP_IC_ENABLE = 0u;
    (void)wait_disabled();
    return TFW_ERR_TIMEOUT;
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    tfw_rp_i2c_timing_t t;

    if (!tfw_rp_i2c_timing((uint32_t)TFW_CM_CPU_HZ, hz, &t)) {
        return TFW_ERR_RANGE;
    }
    TFW_RP_SET(TFW_RP_RESETS_RESET) = TFW_RP_RESET_I2C0;
    TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_I2C0;
    while ((TFW_RP_RESETS_DONE & TFW_RP_RESET_I2C0) == 0u) {
    }

    /* The pad pull-ups, as pico-sdk's examples take them: the Pico 2 W has
     * none on GPIO4/5, so a bare header still idles high. */
    TFW_CM_REG(TFW_RP_PADS_GPIO(TFW_RP_I2C0_SDA_GPIO)) = tfw_rp_pad_value(1u);
    TFW_CM_REG(TFW_RP_PADS_GPIO(TFW_RP_I2C0_SCL_GPIO)) = tfw_rp_pad_value(1u);
    TFW_RP_IO_CTRL(TFW_RP_I2C0_SDA_GPIO) = TFW_RP_FUNC_I2C;
    TFW_RP_IO_CTRL(TFW_RP_I2C0_SCL_GPIO) = TFW_RP_FUNC_I2C;

    /* §12.2.10.2.1: disabled while IC_CON and the counts are written. */
    TFW_RP_IC_ENABLE      = 0u;
    /* Standard mode up to 100 kHz: its tSU;STA is timed from LCNT, fast
     * mode's from HCNT (table 1105), so a standard-mode bus driven in fast
     * mode would get a repeated-START setup below 4.7 us. */
    if (hz <= TFW_RP_I2C_SS_HZ_MAX) {
        TFW_RP_IC_CON         = TFW_RP_IC_CON_MASTER | TFW_RP_IC_CON_SPEED_STD;
        TFW_RP_IC_SS_SCL_HCNT = t.hcnt;
        TFW_RP_IC_SS_SCL_LCNT = t.lcnt;
    } else {
        TFW_RP_IC_CON         = TFW_RP_IC_CON_MASTER | TFW_RP_IC_CON_SPEED_FAST;
        TFW_RP_IC_FS_SCL_HCNT = t.hcnt;
        TFW_RP_IC_FS_SCL_LCNT = t.lcnt;
    }
    TFW_RP_IC_FS_SPKLEN   = t.spklen;
    TFW_RP_IC_INTR_MASK   = 0u;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    uint16_t     total = (uint16_t)(wlen + rlen);
    uint16_t     i;
    uint32_t     cmd;
    uint32_t     raw;
    tfw_status_t st = TFW_OK;

    if (total == 0u) {
        return TFW_OK;
    }
    /* IC_TAR is written only while disabled (table 1110); disabling waits
     * for the master to finish whatever it holds (§12.2.10.3). */
    TFW_RP_IC_ENABLE = 0u;
    if (!wait_disabled()) {
        return abandon();
    }
    TFW_RP_IC_TAR    = (uint32_t)(addr7 & 0x7Fu);
    TFW_RP_IC_ENABLE = TFW_RP_IC_ENABLE_ENABLE;
    (void)TFW_RP_IC_CLR_INTR;

    for (i = 0u; i < total; i++) {
        uint8_t w;

        if (i < wlen) {
            cmd = wr[i];
        } else {
            cmd = TFW_RP_IC_DATA_CMD_CMD_READ;
            if (i == wlen && wlen != 0u) {
                cmd |= TFW_RP_IC_DATA_CMD_RESTART;
            }
        }
        if (i + 1u == total) {
            cmd |= TFW_RP_IC_DATA_CMD_STOP;
        }
        TFW_RP_IC_DATA_CMD = cmd;
        w = wait_cmd(i < wlen ? TFW_RP_IC_STATUS_TFE : TFW_RP_IC_STATUS_RFNE);
        if (w == 2u) {
            return abandon();
        }
        if (w == 0u) {
            break;
        }
        if (i >= wlen) {
            rd[i - wlen] = (uint8_t)(TFW_RP_IC_DATA_CMD & 0xFFu);
        }
    }

    raw = wait_intr(TFW_RP_IC_INTR_TX_ABRT | TFW_RP_IC_INTR_STOP_DET);
    if (raw == 0u) {
        return abandon();
    }
    if ((raw & TFW_RP_IC_INTR_TX_ABRT) != 0u) {
        uint32_t src = TFW_RP_IC_TX_ABRT_SOURCE;

        st = (src & TFW_RP_IC_ABRT_NOACK) != 0u ? TFW_ERR_NACK
           : (src & TFW_RP_IC_ABRT_ARB_LOST) != 0u ? TFW_ERR_BUSY
           : TFW_ERR_TIMEOUT;
        (void)TFW_RP_IC_CLR_TX_ABRT;
        if (wait_intr(TFW_RP_IC_INTR_STOP_DET) == 0u) {
            (void)abandon();
            return st;
        }
    }
    (void)TFW_RP_IC_CLR_STOP_DET;
    return st;
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
