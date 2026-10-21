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
 *   0  magic "TFNC"     4  version (1)     5  flags
 *   6  MAC[6]          12  IP[4]          16  mask[4]
 *  20  gateway[4]      24  endpoint IP[4] 28  endpoint port
 *  30  CRC-16 of bytes 0..29 (the frame CRC), little-endian
 *
 * Flags bit 0 (TFW_NETCFG_DHCP) takes the IP, mask and gateway from a DHCP
 * lease instead of bytes 12..23; the other bits are reserved and ignored.
 */
#define TFW_NETCFG_ADDR     0x00C0u
#define TFW_NETCFG_LEN      32u
#define TFW_NETCFG_VERSION  1u
#define TFW_NETCFG_DHCP     0x01u

typedef struct {
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  mask[4];
    uint8_t  gateway[4];
    uint8_t  peer[4];
    uint16_t port;
    uint8_t  flags;                  /* last, so a positional initialiser
                                        without it reads static (0) */
} tfw_netcfg_t;

/* Decodes a raw block. TFW_ERR_NOT_PROVISIONED for a wrong magic, version or
 * CRC, a zero endpoint or a zero port; *out is written only on TFW_OK. */
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

/* A second socket, UDP, for the DHCP client (src/net/dhcp.c), which keeps no
 * state in the driver: it can come and go while the connection above is up.
 *
 * tfw_eth_udp_open() resets and identifies the controller as tfw_eth_start()
 * does, programs cfg's addresses as given and opens the socket on local_port:
 * the client asking for a first lease. tfw_eth_udp_beside() opens it on
 * local_port with the controller as it is: the client renewing beside the
 * connection. tfw_eth_udp_close() closes it.
 *
 * A datagram is written byte by byte at offset `at` from its start with
 * tfw_eth_udp_put(), then sent whole by tfw_eth_udp_send(): 0 when the socket
 * has no room for len bytes. tfw_eth_udp_sent() says what became of the last
 * one sent. A unicast is sent only once the controller's ARP for its address is
 * answered, so TFW_ETH_SENT_NOARP is the RFC 2131 §2.2 probe's answer: no host
 * holds that address.
 *
 * Received datagrams queue behind the controller's 8-byte header: source IP[4],
 * source port, length, both big-endian. tfw_eth_udp_avail() is how many bytes
 * are queued, tfw_eth_udp_get() the byte at offset `at` from the first unread,
 * and tfw_eth_udp_done() hands len bytes back to the controller. */
#define TFW_ETH_SENT_WAIT   0u
#define TFW_ETH_SENT_OK     1u
#define TFW_ETH_SENT_NOARP  2u

tfw_status_t tfw_eth_udp_open(const tfw_netcfg_t *cfg, uint16_t local_port);
tfw_status_t tfw_eth_udp_beside(uint16_t local_port);
void         tfw_eth_udp_close(void);
void         tfw_eth_udp_put(uint16_t at, uint8_t byte);
uint8_t      tfw_eth_udp_send(uint16_t len, const uint8_t *ip, uint16_t port);
uint8_t      tfw_eth_udp_sent(void);
uint16_t     tfw_eth_udp_avail(void);
uint8_t      tfw_eth_udp_get(uint16_t at);
void         tfw_eth_udp_done(uint16_t len);

/* The address the link connects from: the block's own, or a DHCP lease
 * (RFC 2131) where its flags ask for one. Provided at link time: src/net/dhcp.c
 * in an image with room for the client, src/net/dhcp_none.c elsewhere, which
 * refuses a block that asks for DHCP. Polled by the link while it is down, and
 * never blocks. TFW_OK: cfg's IP, mask and gateway are the ones to use.
 * TFW_ERR_BUSY: still asking, poll again. Anything else: no address on this
 * attempt; the link backs off. */
tfw_status_t tfw_dhcp_poll(tfw_netcfg_t *cfg);

/* The lease the link is connected from, kept beside the connection. While it
 * is up the link asks tfw_dhcp_due(), which is cheap and always 0 for a static
 * address; only when it is 1 does the link load the block and call
 * tfw_dhcp_renew(), which renews or rebinds the lease (RFC 2131 §4.4.5) and
 * never blocks. TFW_OK: the lease holds. Anything else: it is lost; the link
 * drops the connection and its next poll asks again. */
uint8_t      tfw_dhcp_due(void);
tfw_status_t tfw_dhcp_renew(const tfw_netcfg_t *cfg);

#endif /* TFW_ETH_H */
