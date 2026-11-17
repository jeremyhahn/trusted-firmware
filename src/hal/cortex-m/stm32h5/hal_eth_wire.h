/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-wire over TCP on the STM32H5's own MAC (#582, the ETH_MAC=1 image).
 *
 * hal_eth_wire.c is the image's tfw/link.h: the UART and one TCP connection
 * feed the one transport. It knows nothing of lwIP; hal_eth_wire_tcp.c is
 * the lwIP raw-API listener behind it and the seam between them is below, so
 * the link logic is host-tested over a fake (test/unit/test_h5_eth_wire.c).
 */
#ifndef TFW_H5_ETH_WIRE_H
#define TFW_H5_ETH_WIRE_H

#include <stdint.h>

/* The port the board listens on: the ESP32 port's wire port, so one host
 * transport (tfwctl PORT=tcp:<addr>:21574) reaches both. */
#define TFW_H5_WIRE_PORT 21574u

/* Bytes the link holds from the connection before the transport reads them.
 * At least lwIP's TCP_WND: the window is reopened only as bytes are read, so
 * a peer never has more in flight than the ring has room for. */
#define TFW_H5_WIRE_RX_RING 2048u

/* Reply bytes batched before they are handed to TCP. */
#define TFW_H5_WIRE_TX_BUF 512u

/* --- called by the listener (hal_eth_wire_tcp.c) -------------------------- */

/* A connection was accepted: whatever the last one left is discarded. */
void     tfw_h5_wire_open(void);

/* The connection has gone (closed, reset or replaced). */
void     tfw_h5_wire_drop(void);

/* Bytes received; returns how many were taken (all of them, unless the peer
 * overran the window). */
uint16_t tfw_h5_wire_push(const uint8_t *buf, uint16_t len);

/* --- provided by the listener ------------------------------------------- */

/* Starts listening on TFW_H5_WIRE_PORT; called once lwIP is up. */
void     tfw_h5_wire_listen(void);

/* Queues and sends up to len bytes on the connection; returns how many were
 * taken (0 when its send buffer is full). */
uint16_t tfw_h5_wire_send(const uint8_t *buf, uint16_t len);

/* The transport has read n bytes: reopen the receive window by n. */
void     tfw_h5_wire_recved(uint16_t n);

#endif /* TFW_H5_ETH_WIRE_H */
