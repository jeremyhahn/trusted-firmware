/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The transport's byte link, substituted at link time like the HAL.
 *
 * A serial-only image links src/proto/link_serial.c: the UART and nothing
 * else. An Ethernet image links src/proto/link_eth.c, which serves the UART and
 * a W5x00 socket to the same transport. The transport keeps one framer and one
 * response buffer either way; the link decides which wire a byte came from and
 * which one its reply goes back on.
 *
 * Nothing here blocks.
 */
#ifndef TFW_LINK_H
#define TFW_LINK_H

#include <stdint.h>

/* Advances whatever the link runs between bytes -- for Ethernet, the socket
 * state and the reconnect schedule. Called once per transport pass. */
void    tfw_link_poll(void);

/* 1 if a byte was taken. hold is set while a frame is partly received: the
 * link then reads only the wire that frame arrived on, so two hosts' frames
 * never interleave in the one framer. */
uint8_t tfw_link_rx(uint8_t *out, uint8_t hold);

/* 1 if the byte was taken, 0 to retry on a later pass. A reply goes back on
 * the wire its request came from; serial set sends on the UART regardless,
 * which is where events go. A reply owed to a wire that has gone is taken and
 * dropped, so a dead peer never holds the transport. */
uint8_t tfw_link_tx_try(uint8_t byte, uint8_t serial);

/* The response or event just written is complete. */
void    tfw_link_tx_done(uint8_t serial);

/* 1 if the wire the current frame arrived on may run cmd. */
uint8_t tfw_link_admits(uint8_t cmd);

#endif /* TFW_LINK_H */
