/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ETH_MAC=1 image's link (#582): tfw-wire on the UART and on one TCP
 * connection to the board, in place of link_serial.c. The connection is
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
 *     taken and dropped, so a dead peer never holds the transport; nor
 *     does a live one that stops reading: a reply it takes none of for
 *     TFW_H5_WIRE_TX_STALL_MS resets the connection and is dropped.
 */
#include <stdint.h>
#include <string.h>

#include "tfw/link.h"
#include "tfw/hal.h"
#include "hal_eth_wire.h"
#include "link_eth.h"

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
static uint8_t  peer_eof;       /* the peer sent FIN: finish, then close */
static uint8_t  tx_stalled;     /* the last send took nothing ... */
static tfw_ms_t tx_stall_since; /* ... and since when */

/* Diagnostic text (tfw_h5_wire_say) waits here while a transport frame is
 * partly in the UART ring, and goes out only between frames (#584). */
static uint8_t say_buf[TFW_H5_WIRE_SAY_BUF];
static uint8_t say_len, say_pos;
static uint8_t uart_frame;              /* a frame's first byte is out, its end is not */

static uint8_t say_flush(void)
{
    while (say_pos < say_len) {
        if (!tfw_hal_uart_tx_try(say_buf[say_pos])) {
            return 0u;
        }
        say_pos++;
    }
    say_len = 0u;
    say_pos = 0u;
    return 1u;
}

static void wire_reset(void)
{
    rx_head    = 0u;
    rx_tail    = 0u;
    rx_unacked = 0u;
    tx_len     = 0u;
    peer_eof   = 0u;
    tx_stalled = 0u;
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

void tfw_h5_wire_eof(void)
{
    if (up) {
        peer_eof = 1u;
    }
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
            /* Send buffer full: the next poll retries, within a fixed bound.
             * A peer that takes nothing for TFW_H5_WIRE_TX_STALL_MS is reset,
             * and the reply owed to it is dropped. */
            tfw_ms_t now = tfw_hal_millis();
            if (!tx_stalled) {
                tx_stalled     = 1u;
                tx_stall_since = now;
            } else if ((tfw_ms_t)(now - tx_stall_since) >= TFW_H5_WIRE_TX_STALL_MS) {
                tfw_h5_wire_abort();
            }
            return;
        }
        tx_stalled = 0u;
        if (n > tx_len) {
            n = tx_len;
        }
        memmove(tx_buf, tx_buf + n, (size_t)(tx_len - n));
        tx_len = (uint16_t)(tx_len - n);
    }
    tx_stalled = 0u;
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
    /* A peer that sent FIN is closed once what it sent is read and answered:
     * the transport reads and answers a frame in one poll, so an empty ring
     * and an empty batch at the top of the next poll mean nothing is owed. */
    if (peer_eof && rx_tail == rx_head && tx_len == 0u) {
        tfw_h5_wire_finish();
    }
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
        /* A frame's first byte waits until every queued diagnostic is in the
         * UART ring, so diagnostic text never lands inside a frame. */
        if (!uart_frame) {
            if (!say_flush()) {
                return 0u;
            }
            uart_frame = 1u;
        }
        return tfw_hal_uart_tx_try(byte);
    }
    if (!up) {
        return 1u;                      /* the peer has gone: drop the reply */
    }
    if (tx_len == TFW_H5_WIRE_TX_BUF) {
        wire_flush();
        if (!up) {
            return 1u;                  /* reset for stalling: drop the reply */
        }
        if (tx_len == TFW_H5_WIRE_TX_BUF) {
            return 0u;
        }
    }
    tx_buf[tx_len++] = byte;
    return 1u;
}

void tfw_link_tx_done(uint8_t serial)
{
    uart_frame = 0u;                    /* the transport holds one frame at a time */
    if (!serial && cur == WIRE_TCP && up) {
        wire_flush();
    }
    (void)say_flush();
}

void tfw_h5_wire_say(const char *s)
{
    uint8_t n = 0u;

    while (s[n] != '\0' && n < TFW_H5_WIRE_SAY_BUF) {
        n++;
    }
    if (s[n] != '\0' || n > (uint8_t)(TFW_H5_WIRE_SAY_BUF - say_len)) {
        return;                         /* no room for the whole line: dropped */
    }
    for (uint8_t i = 0u; i < n; i++) {
        say_buf[say_len++] = (uint8_t)s[i];
    }
    if (!uart_frame) {
        (void)say_flush();
    }
}

void tfw_h5_wire_say_poll(void)
{
    if (!uart_frame) {
        (void)say_flush();
    }
}

uint8_t tfw_link_admits(uint8_t cmd)
{
    /* The UART carries every command; a TCP peer only the Ethernet
     * allow-list (link_eth.h): device control and provisioning stay on
     * serial (SPEC, the STM32H5 Ethernet ADR). */
    if (cur == WIRE_SERIAL) {
        return 1u;
    }
    return tfw_link_eth_allows(cmd);
}

uint8_t tfw_link_take_reset(void)
{
    uint8_t r = peer_changed;

    peer_changed = 0u;
    return r;
}
