/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw/eth.h on a WIZnet W5100 or W5500: socket 0 as a TCP client. The chip
 * holds the whole TCP/IP stack; this file programs registers and moves bytes
 * through the socket's buffers, reaching the chip only through the three
 * functions of w5x00.h that the linked chip file provides.
 *
 * Receive works in batches: the size the chip reports is taken once, read
 * byte by byte, and handed back with RECV when the batch is spent. Transmit
 * fills the buffer from the chip's write pointer and commits it with SEND.
 */

#include "tfw/eth.h"
#include "tfw/board.h"
#include "w5x00.h"

/* Mode 0, MSB first, fosc/4: the controller's bus, whatever a host last set. */
#define W5X_SPI_MODE    0u
#define W5X_SPI_CLKDIV  0u

static uint16_t rx_rd;       /* next byte to read in the receive buffer */
static uint16_t rx_left;     /* bytes of the current batch still unread */
static uint16_t tx_wr;       /* next free byte in the transmit buffer */
static uint16_t tx_room;     /* free bytes from tx_wr; 0 means ask the chip */
static uint8_t  tx_queued;   /* bytes written since the last SEND */
static uint8_t  tx_sending;  /* a SEND has not yet been seen to finish */

tfw_status_t w5x_bus_init(tfw_pinref_t *cs)
{
    tfw_pinref_t sd;

    /* The SD card shares the bus and answers whenever its select floats low,
     * so it is parked high first -- level before direction, so the pin never
     * drives low on the way. */
    if (tfw_board_pin(TFW_ETH_SD_CS_PIN, &sd) != TFW_OK ||
        tfw_board_pin(TFW_ETH_CS_PIN, cs) != TFW_OK) {
        return TFW_ERR_UNSUPPORTED;
    }
    (void)tfw_hal_gpio_write(&sd, 1u);
    (void)tfw_hal_gpio_mode(&sd, TFW_PIN_OUTPUT);
    return TFW_OK;
}

void w5x_xfer(const tfw_pinref_t *cs, uint8_t *buf, uint8_t len)
{
    (void)tfw_hal_spi_init(W5X_SPI_MODE, W5X_SPI_CLKDIV, 0u);
    (void)tfw_hal_spi_xfer(cs, buf, buf, len);
}

static uint16_t rd16(uint16_t off)
{
    return (uint16_t)((uint16_t)w5x_rd(W5X_SOCK0, off) << 8 |
                      w5x_rd(W5X_SOCK0, (uint16_t)(off + 1u)));
}

static void wr16(uint16_t off, uint16_t v)
{
    w5x_wr(W5X_SOCK0, off, (uint8_t)(v >> 8));
    w5x_wr(W5X_SOCK0, (uint16_t)(off + 1u), (uint8_t)v);
}

/* The free and received sizes change under the reader as the chip works, and
 * the two bytes are separate reads -- so both chips' datasheets say to read
 * until two agree. */
static uint16_t rd16_stable(uint16_t off)
{
    uint16_t a = rd16(off);
    uint8_t  n;

    for (n = 0u; n < W5X_POLL_LIMIT; n++) {
        uint16_t b = rd16(off);

        if (a == b) {
            return a;
        }
        a = b;
    }
    return 0u;                       /* never settled: treat as nothing */
}

static void write_n(uint8_t space, uint16_t off, const uint8_t *v, uint8_t n)
{
    uint8_t i;

    for (i = 0u; i < n; i++) {
        w5x_wr(space, (uint16_t)(off + i), v[i]);
    }
}

/* Issues a socket command and waits, boundedly, for the chip to take it. */
static uint8_t command(uint8_t cr)
{
    uint8_t n;

    w5x_wr(W5X_SOCK0, W5X_SN_CR, cr);
    for (n = 0u; n < W5X_POLL_LIMIT; n++) {
        if (w5x_rd(W5X_SOCK0, W5X_SN_CR) == 0u) {
            return 1u;
        }
    }
    return 0u;
}

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    tfw_status_t st = w5x_chip_init();

    if (st != TFW_OK) {
        return st;
    }
    write_n(W5X_COMMON, W5X_GAR,  cfg->gateway, 4u);
    write_n(W5X_COMMON, W5X_SUBR, cfg->mask,    4u);
    write_n(W5X_COMMON, W5X_SHAR, cfg->mac,     6u);
    write_n(W5X_COMMON, W5X_SIPR, cfg->ip,      4u);

    w5x_wr(W5X_SOCK0, W5X_SN_MR, W5X_SN_MR_TCP);
    wr16(W5X_SN_PORT, local_port);
    if (!command(W5X_CR_OPEN) || w5x_rd(W5X_SOCK0, W5X_SN_SR) != W5X_SR_INIT) {
        (void)command(W5X_CR_CLOSE);
        return TFW_ERR_INTERNAL;
    }
    write_n(W5X_SOCK0, W5X_SN_DIPR, cfg->peer, 4u);
    wr16(W5X_SN_DPORT, cfg->port);
    rx_left    = 0u;
    tx_room    = 0u;
    tx_queued  = 0u;
    tx_sending = 0u;
    if (!command(W5X_CR_CONNECT)) {
        (void)command(W5X_CR_CLOSE);
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

uint8_t tfw_eth_state(void)
{
    switch (w5x_rd(W5X_SOCK0, W5X_SN_SR)) {
    case W5X_SR_ESTABLISHED:
        return TFW_ETH_UP;
    case W5X_SR_INIT:
    case W5X_SR_SYNSENT:
        return TFW_ETH_BUSY;
    default:
        /* CLOSED after a refused or timed-out connect, CLOSE_WAIT once the
         * peer has hung up, or a closing state on the way to either. Unread
         * bytes from a peer that has gone are of no use to anyone. */
        tfw_eth_close();
        return TFW_ETH_DOWN;
    }
}

uint8_t tfw_eth_rx(uint8_t *out)
{
    if (rx_left == 0u) {
        rx_left = rd16_stable(W5X_SN_RX_RSR);
        if (rx_left == 0u) {
            return 0u;
        }
        if (rx_left > W5X_BUF_SIZE) {
            /* More than the ring holds, which no genuine part reports: a
             * glitched or counterfeit chip or a bus fault. Reading it would
             * go round the ring and hand up bytes never received, and no
             * part of it can be trusted, so the link is dropped as a hang-up
             * is and the host reconnects. */
            tfw_eth_close();
            return 0u;
        }
        rx_rd = rd16(W5X_SN_RX_RD);
    }
    *out = w5x_rd(W5X_RX0, rx_rd);
    rx_rd++;
    if (--rx_left == 0u) {
        wr16(W5X_SN_RX_RD, rx_rd);
        (void)command(W5X_CR_RECV);
    }
    return 1u;
}

uint8_t tfw_eth_tx(uint8_t byte)
{
    if (tx_room == 0u) {
        if (tx_queued) {
            tfw_eth_send();          /* a full buffer: send what is there */
        }
        tx_room = rd16_stable(W5X_SN_TX_FSR);
        if (tx_room == 0u) {
            return 0u;
        }
        tx_wr = rd16(W5X_SN_TX_WR);
    }
    w5x_wr(W5X_TX0, tx_wr, byte);
    tx_wr++;
    tx_room--;
    tx_queued = 1u;
    return 1u;
}

void tfw_eth_send(void)
{
    if (!tx_queued) {
        return;
    }
    /* A SEND may not be issued over one still in flight. The previous one is a
     * whole request-response ago, so this almost never waits; when it would
     * wait past the bound, the connection is in trouble and the state poll
     * will see it go. */
    if (tx_sending) {
        uint8_t n;

        for (n = 0u; n < W5X_POLL_LIMIT; n++) {
            if (w5x_rd(W5X_SOCK0, W5X_SN_IR) & (W5X_IR_SEND_OK | W5X_IR_TIMEOUT | W5X_IR_DISCON)) {
                break;
            }
        }
    }
    w5x_wr(W5X_SOCK0, W5X_SN_IR, W5X_IR_SEND_OK);
    wr16(W5X_SN_TX_WR, tx_wr);
    (void)command(W5X_CR_SEND);
    tx_queued  = 0u;
    tx_sending = 1u;
    tx_room    = 0u;                 /* re-read from the chip after SEND */
}

void tfw_eth_close(void)
{
    (void)command(W5X_CR_CLOSE);
    rx_left   = 0u;
    tx_room   = 0u;
    tx_queued = 0u;
}
