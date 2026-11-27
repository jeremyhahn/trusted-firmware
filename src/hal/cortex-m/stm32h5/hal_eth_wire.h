/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-wire over TCP on the STM32H5's own MAC (#582, the ETH_MAC=1 image); the device opens
 * the connection (#584).
 *
 * hal_eth_wire.c is the image's tfw/link.h: the UART and one TCP connection
 * feed the one transport. It knows nothing of lwIP; hal_eth_wire_tcp.c is
 * the lwIP raw-API connection behind it and the seam between them is below, so
 * the link logic is host-tested over a fake (test/unit/test_h5_eth_wire.c).
 */
#ifndef TFW_H5_ETH_WIRE_H
#define TFW_H5_ETH_WIRE_H

#include <stdint.h>

#include "tfw/eth.h"

/* The device opens the one connection to the endpoint in its network block
 * (include/tfw/eth.h) and listens on nothing. A refused or dropped
 * connection is dialled again after TFW_H5_WIRE_REDIAL_MS, the wait doubling
 * on each refusal up to TFW_H5_WIRE_REDIAL_MAX_MS, the block read afresh
 * before each dial. */
#define TFW_H5_WIRE_REDIAL_MS     1000u
#define TFW_H5_WIRE_REDIAL_MAX_MS 16000u
/* Keepalive on the open connection: a peer silent for KEEP_IDLE is probed
 * KEEP_CNT times KEEP_INTVL apart, and the connection is aborted and
 * redialled when none is answered (30 s in all). */
#define TFW_H5_WIRE_KEEP_IDLE_MS  15000u
#define TFW_H5_WIRE_KEEP_INTVL_MS 5000u
#define TFW_H5_WIRE_KEEP_CNT      3u

/* Size of the ring that holds bytes from the connection until the transport
 * reads them. The ring keeps one byte free, so it holds
 * TFW_H5_WIRE_RX_RING - 1 bytes, and that must be at least lwIP's TCP_WND:
 * the window is reopened only as bytes are read, so a peer never has more in
 * flight than the ring has room for. Both bounds are checked at build time:
 * the power-of-two size in hal_eth_wire.c, the window in hal_eth_wire_tcp.c. */
#define TFW_H5_WIRE_RX_RING 2048u

/* Reply bytes batched before they are handed to TCP. */
#define TFW_H5_WIRE_TX_BUF 512u

/* How long a reply may wait on a TCP peer that takes none of it (its send
 * buffer full, the peer not reading) before the connection is reset, so a
 * peer that stops reading never holds the transport and the UART (#584). */
#define TFW_H5_WIRE_TX_STALL_MS 2000u

/* --- diagnostics on the UART (hal_eth_netif.c) ----------------------------- */

#ifndef TFW_H5_WIRE_SAY_BUF
#define TFW_H5_WIRE_SAY_BUF 64u
#endif

/* Queues one diagnostic line for the UART. It goes out only between transport
 * frames, never inside one; a line with no room left is dropped whole. */
void     tfw_h5_wire_say(const char *s);

/* Sends queued diagnostics when no frame is in flight; called each poll. */
void     tfw_h5_wire_say_poll(void);

/* --- the interface (hal_eth_netif.c), for the connection and the signer ---- */

/* Brings the interface to the network block just loaded before a dial: a
 * changed address, mask or gateway is set, a switch between DHCP and a static
 * address stops one and starts the other, and a new MAC is programmed (the
 * link then comes up again), so a block written over serial takes effect
 * without a reset (include/tfw/eth.h). Returns 1 when the interface is up on
 * that block with an address and the dial may go ahead, 0 otherwise. */
int      tfw_h5_eth_reconcile(const tfw_netcfg_t *cfg);

/* Runs the lwIP timers and drops an expired lease's address, and nothing
 * else: no frame is read and no request is dispatched. Called each poll, and
 * from tfw_sign_yield() so a long software signature keeps them on time. */
void     tfw_h5_eth_service_deadlines(void);
/* Reads the received frames into lwIP, as each poll does, and nothing else:
 * TCP segments (a keepalive answer, an ACK) are processed, and data is only
 * buffered by the wire's receive callback, never dispatched. Called from
 * tfw_sign_yield() so a peer answering keepalive during a long software
 * signature is not found dead. */
void     tfw_h5_eth_service_rx_nodispatch(void);

/* --- called by the connection (hal_eth_wire_tcp.c) -------------------------- */

/* A connection was accepted: whatever the last one left is discarded. */
void     tfw_h5_wire_open(void);

/* The connection has gone (closed, reset or replaced). */
void     tfw_h5_wire_drop(void);

/* Bytes received; returns how many were taken (all of them, unless the peer
 * overran the window). */
uint16_t tfw_h5_wire_push(const uint8_t *buf, uint16_t len);

/* The peer sent FIN: what it sent is still read and answered; the link then
 * calls tfw_h5_wire_finish(). */
void     tfw_h5_wire_eof(void);

/* --- provided by the connection ------------------------------------------- */

/* Dials the network block's endpoint when no connection is open or being
 * opened and the backoff has run; called each poll once the netif has an
 * address. A missing or bad block dials nothing. */
void     tfw_h5_wire_dial_poll(void);

/* Queues and sends up to len bytes on the connection; returns how many were
 * taken (0 when its send buffer is full). */
uint16_t tfw_h5_wire_send(const uint8_t *buf, uint16_t len);

/* The transport has read n bytes: reopen the receive window by n. */
void     tfw_h5_wire_recved(uint16_t n);

/* The peer has taken no reply bytes for TFW_H5_WIRE_TX_STALL_MS: reset the
 * connection now (the link forgets it at once, as tfw_h5_wire_drop). */
void     tfw_h5_wire_abort(void);

/* A half-closed peer has nothing left to read or answer: close the
 * connection once its send queue has drained (a later poll retries). */
void     tfw_h5_wire_finish(void);

#endif /* TFW_H5_ETH_WIRE_H */
