/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L's I2C master: TWIM21 (soc_regs.h), polled, INTEN 0, EasyDMA one
 * transfer per part of a transaction -- the write from a RAM copy, the read
 * straight into the caller's buffer -- and the repeated START between them by
 * the LASTTX_DMA_RX_START shortcut. The STOP is the LASTTX_STOP or
 * LASTRX_STOP shortcut on success and TASKS_STOP on an error, the sequence
 * nrfx's twim driver uses (RESUME, then STOP), so every return that reached
 * the bus sent one. Status codes as the MPS2 master's: an address or data NACK
 * (ERRORSRC ANACK, DNACK) is TFW_ERR_NACK, a transfer whose STOPPED event never
 * comes is TFW_ERR_TIMEOUT after TASKS_STOP.
 *
 * SPI and 1-Wire are not on this board: their entry points refuse, since
 * src/caps/bus.c is one translation unit and links all three.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "soc_regs.h"

/* The longest transaction is 255 bytes out and 255 in, about 46 ms at
 * 100 kHz; the wait allows twice that, polled once a microsecond. */
#define TWIM_WAIT_US 100000uL

/* The write's RAM copy: EasyDMA reads its buffer from RAM, and the caller's
 * bytes may be constants. */
static uint8_t s_txbuf[255];

static void twim_clear_events(void)
{
    TFW_NRF_TWIM_EV_STOPPED = 0u;
    TFW_NRF_TWIM_EV_ERROR   = 0u;
    TFW_NRF_TWIM_EV_LASTTX  = 0u;
    TFW_NRF_TWIM_EV_LASTRX  = 0u;
    TFW_NRF_TWIM_ERRORSRC   = TFW_NRF_TWIM_ERRORSRC_ALL;
}

/* Waits for STOPPED. An ERROR stops the transfer with TASKS_STOP, and its
 * cause is kept for the status. */
static tfw_status_t twim_wait(void)
{
    uint32_t     n;
    uint32_t     src;
    tfw_status_t st = TFW_OK;

    for (n = 0u; n < TWIM_WAIT_US; n++) {
        if (TFW_NRF_TWIM_EV_STOPPED != 0u) {
            break;
        }
        if (st == TFW_OK && TFW_NRF_TWIM_EV_ERROR != 0u) {
            TFW_NRF_TWIM_EV_ERROR = 0u;
            src = TFW_NRF_TWIM_ERRORSRC;
            TFW_NRF_TWIM_ERRORSRC = TFW_NRF_TWIM_ERRORSRC_ALL;
            st = (src & (TFW_NRF_TWIM_ERRORSRC_ANACK | TFW_NRF_TWIM_ERRORSRC_DNACK)) != 0u
                     ? TFW_ERR_NACK : TFW_ERR_INTERNAL;
            TFW_NRF_TWIM_RESUME = 1u;
            TFW_NRF_TWIM_STOP   = 1u;
        }
        tfw_hal_delay_us(1u);
    }
    TFW_NRF_TWIM_SHORTS = 0u;
    if (n == TWIM_WAIT_US) {
        TFW_NRF_TWIM_STOP = 1u;
        return TFW_ERR_TIMEOUT;
    }
    TFW_NRF_TWIM_EV_STOPPED = 0u;
    return st;
}

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    uint32_t freq;

    /* The fastest MDK rate not above the one asked for; nothing below
     * 100 kHz, which the TWIM has no setting for. */
    if (hz >= 400000uL) {
        freq = TFW_NRF_TWIM_FREQUENCY_K400;
    } else if (hz >= 250000uL) {
        freq = TFW_NRF_TWIM_FREQUENCY_K250;
    } else if (hz >= 100000uL) {
        freq = TFW_NRF_TWIM_FREQUENCY_K100;
    } else {
        return TFW_ERR_RANGE;
    }
    TFW_NRF_TWIM_ENABLE = 0u;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_TWIM_SCL_BIT) = TFW_NRF_PIN_CNF_TWI;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, TFW_NRF_TWIM_SDA_BIT) = TFW_NRF_PIN_CNF_TWI;
    TFW_NRF_TWIM_PSEL_SCL  = (TFW_NRF_TWIM_PORT << TFW_NRF_TWIM_PSEL_PORT_SHIFT) | TFW_NRF_TWIM_SCL_BIT;
    TFW_NRF_TWIM_PSEL_SDA  = (TFW_NRF_TWIM_PORT << TFW_NRF_TWIM_PSEL_PORT_SHIFT) | TFW_NRF_TWIM_SDA_BIT;
    TFW_NRF_TWIM_FREQUENCY = freq;
    TFW_NRF_TWIM_INTEN     = 0u;
    TFW_NRF_TWIM_SHORTS    = 0u;
    TFW_NRF_TWIM_ENABLE    = TFW_NRF_TWIM_ENABLE_ON;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                            uint8_t *rd, uint8_t rlen)
{
    if (wlen == 0u && rlen == 0u) {
        return TFW_OK;
    }
    twim_clear_events();
    TFW_NRF_TWIM_ADDRESS = (uint32_t)addr7;
    if (rlen) {
        TFW_NRF_TWIM_RX_PTR    = (uint32_t)(uintptr_t)rd;
        TFW_NRF_TWIM_RX_MAXCNT = rlen;
    }
    if (wlen) {
        uint8_t i;

        for (i = 0u; i < wlen; i++) {
            s_txbuf[i] = wr[i];
        }
        TFW_NRF_TWIM_TX_PTR    = (uint32_t)(uintptr_t)s_txbuf;
        TFW_NRF_TWIM_TX_MAXCNT = wlen;
        TFW_NRF_TWIM_SHORTS    = rlen ? (TFW_NRF_TWIM_SHORTS_LASTTX_RXSTART
                                         | TFW_NRF_TWIM_SHORTS_LASTRX_STOP)
                                      : TFW_NRF_TWIM_SHORTS_LASTTX_STOP;
        TFW_NRF_TWIM_TX_START  = 1u;
    } else {
        TFW_NRF_TWIM_SHORTS    = TFW_NRF_TWIM_SHORTS_LASTRX_STOP;
        TFW_NRF_TWIM_RX_START  = 1u;
    }
    return twim_wait();
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
