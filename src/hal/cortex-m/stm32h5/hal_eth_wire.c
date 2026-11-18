/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ETH_MAC=1 image's link (#582): tfw-wire on the UART and on one TCP
 * connection to the board, in place of link_serial.c. The listener is
 * hal_eth_wire_tcp.c (lwIP raw API, NO_SYS); this file is the part the
 * transport sees, as link_eth.c is for the W5x00:
 *
 *   - a frame is read whole from the wire it started on (hold), and its
 *     reply goes back on that wire; events always go on the UART;
 *   - every command is admitted on either wire: the TCP wire is the bench
 *     host's whole wire, as the ESP32 port's is;
 *   - received bytes wait in a ring and the receive window is reopened only
 *     as the transport reads them, so the ring never overflows;
 *   - reply bytes are batched and handed to TCP when the reply is complete
 *     (or the batch is full); a reply owed to a connection that has gone is
 *     taken and dropped, so a dead peer never holds the transport.
 */
#include <stdint.h>
#include <string.h>

#include "tfw/link.h"
#include "tfw/hal.h"
#include "hal_eth_wire.h"

enum { WIRE_SERIAL = 0u, WIRE_TCP = 1u };

#define RX_MASK (TFW_H5_WIRE_RX_RING - 1u)

static uint8_t  rx_ring[TFW_H5_WIRE_RX_RING];
static uint16_t rx_head;
static uint16_t rx_tail;
static uint16_t rx_unacked;             /* read, window not yet reopened */
static uint8_t  tx_buf[TFW_H5_WIRE_TX_BUF];
static uint16_t tx_len;
static uint8_t  up;
static uint8_t  cur = WIRE_SERIAL;
static uint8_t  peer_changed;   /* for tfw_link_take_reset() */

static void wire_reset(void)
{
    rx_head    = 0u;
    rx_tail    = 0u;
    rx_unacked = 0u;
    tx_len     = 0u;
    /* The transport's half of the old peer -- a partly decoded frame, a
     * reply still owed -- goes too, but only when TCP is the current wire:
     * a UART frame in flight is not the TCP peer's to discard. */
    if (cur == WIRE_TCP) {
        peer_changed = 1u;
    }
}

void tfw_h5_wire_open(void)
{
    wire_reset();
    up = 1u;
}

void tfw_h5_wire_drop(void)
{
    wire_reset();
    up = 0u;
}

uint16_t tfw_h5_wire_push(const uint8_t *buf, uint16_t len)
{
    uint16_t used = (uint16_t)((rx_head - rx_tail) & RX_MASK);
    uint16_t room = (uint16_t)(RX_MASK - used);
    uint16_t n    = (len < room) ? len : room;

    if (!up) {
        return 0u;
    }
    for (uint16_t i = 0u; i < n; i++) {
        rx_ring[rx_head] = buf[i];
        rx_head          = (uint16_t)((rx_head + 1u) & RX_MASK);
    }
    return n;
}

static void wire_flush(void)
{
    while (tx_len > 0u) {
        uint16_t n = tfw_h5_wire_send(tx_buf, tx_len);
        if (n == 0u) {
            return;                     /* send buffer full: next poll */
        }
        if (n > tx_len) {
            n = tx_len;
        }
        memmove(tx_buf, tx_buf + n, (size_t)(tx_len - n));
        tx_len = (uint16_t)(tx_len - n);
    }
}

void tfw_link_poll(void)
{
    if (!up) {
        return;
    }
    if (rx_unacked > 0u) {
        tfw_h5_wire_recved(rx_unacked);
        rx_unacked = 0u;
    }
    wire_flush();
}

static uint8_t tcp_rx(uint8_t *out)
{
    if (!up || rx_tail == rx_head) {
        return 0u;
    }
    *out    = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) & RX_MASK);
    rx_unacked++;
    return 1u;
}

uint8_t tfw_link_rx(uint8_t *out, uint8_t hold)
{
    if (hold) {
        return (cur == WIRE_TCP) ? tcp_rx(out) : tfw_hal_uart_rx(out);
    }
    if (cur == WIRE_SERIAL) {
        if (tcp_rx(out)) {
            cur = WIRE_TCP;
            return 1u;
        }
        return tfw_hal_uart_rx(out);
    }
    if (tfw_hal_uart_rx(out)) {
        cur = WIRE_SERIAL;
        return 1u;
    }
    return tcp_rx(out);
}

uint8_t tfw_link_tx_try(uint8_t byte, uint8_t serial)
{
    if (serial || cur == WIRE_SERIAL) {
        return tfw_hal_uart_tx_try(byte);
    }
    if (!up) {
        return 1u;                      /* the peer has gone: drop the reply */
    }
    if (tx_len == TFW_H5_WIRE_TX_BUF) {
        wire_flush();
        if (tx_len == TFW_H5_WIRE_TX_BUF) {
            return 0u;
        }
    }
    tx_buf[tx_len++] = byte;
    return 1u;
}

void tfw_link_tx_done(uint8_t serial)
{
    if (!serial && cur == WIRE_TCP && up) {
        wire_flush();
    }
}

uint8_t tfw_link_admits(uint8_t cmd)
{
    (void)cmd;
    return 1u;
}

uint8_t tfw_link_take_reset(void)
{
    uint8_t r = peer_changed;

    peer_changed = 0u;
    return r;
}
