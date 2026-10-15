/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * One TCP socket the device opens to a provisioned endpoint, and the network
 * block in EEPROM that says where.
 *
 * Provided at link time, like the HAL: src/net/w5x00.c with one chip file
 * (w5100.c or w5500.c) in a device image, tools/tfwsim/eth_socket.c over a real
 * host socket in the simulator. src/proto/link_eth.c is the only caller.
 *
 * Nothing here blocks. A controller command is polled a bounded number of
 * times; the connect and its retries run in the controller, and the caller
 * reads the outcome from tfw_eth_state() on a later pass.
 */
#ifndef TFW_ETH_H
#define TFW_ETH_H

#include <stdint.h>
#include "tfw/status.h"

/* The network block: 32 bytes at a fixed EEPROM address, the first after the
 * platform block, so it is the same on every board and moves no provisioning
 * vector. Written over serial with EEPROM_WRITE. Layout, multi-byte fields
 * big-endian except the CRC:
 *
 *   0  magic "TFNC"     4  version (1)     5  flags (0)
 *   6  MAC[6]          12  IP[4]          16  mask[4]
 *  20  gateway[4]      24  endpoint IP[4] 28  endpoint port
 *  30  CRC-16 of bytes 0..29 (the frame CRC), little-endian
 */
#define TFW_NETCFG_ADDR     0x00C0u
#define TFW_NETCFG_LEN      32u
#define TFW_NETCFG_VERSION  1u

typedef struct {
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  mask[4];
    uint8_t  gateway[4];
    uint8_t  peer[4];
    uint16_t port;
} tfw_netcfg_t;

/* Decodes a raw block. TFW_ERR_NOT_PROVISIONED for a wrong magic, version or
 * CRC, a zero endpoint or a zero port. */
tfw_status_t tfw_netcfg_decode(const uint8_t *raw, tfw_netcfg_t *out);

/* Reads the block from EEPROM and decodes it. Never cached: a block written
 * over serial is picked up at the next connection attempt. */
tfw_status_t tfw_netcfg_load(tfw_netcfg_t *out);

#define TFW_ETH_DOWN  0u   /* no connection; tfw_eth_start() again */
#define TFW_ETH_BUSY  1u   /* connecting */
#define TFW_ETH_UP    2u   /* established */

/* Resets and identifies the controller, programs the addresses and starts a
 * TCP connect from local_port. TFW_ERR_UNSUPPORTED if the controller is absent
 * or is not the one this image was built for; nothing has then been written. */
tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port);

/* The socket's state. A connection the peer has closed is closed here and
 * reads TFW_ETH_DOWN. Only meaningful after a successful tfw_eth_start(). */
uint8_t      tfw_eth_state(void);

/* 1 if a received byte was taken. */
uint8_t      tfw_eth_rx(uint8_t *out);

/* 1 if the byte was queued for sending, 0 if the transmit buffer is full. */
uint8_t      tfw_eth_tx(uint8_t byte);

/* Sends what tfw_eth_tx() has queued. */
void         tfw_eth_send(void);

/* Drops the connection. */
void         tfw_eth_close(void);

#endif /* TFW_ETH_H */
