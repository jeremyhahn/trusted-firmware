/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What src/net/w5x00.c needs from a chip file, and the register facts the two
 * chips share.
 *
 * The W5100 and W5500 have the same common registers and the same socket
 * registers at the same offsets. They differ in SPI framing, in where socket
 * 0's registers and buffers sit, and in how each identifies itself. So a chip
 * file is three functions over a register SPACE and an offset, and the socket
 * logic is written once above them. One chip file is linked per image, chosen
 * by the board's ETH_CHIP: no dispatch between them, no vtable.
 */
#ifndef TFW_W5X00_H
#define TFW_W5X00_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/hal.h"

/* Register spaces. The numbers are the W5500's block selects; the W5100 file
 * maps them onto its flat address map. In the buffer spaces the offset is the
 * socket's raw 16-bit pointer, and the chip file wraps it into the buffer. */
#define W5X_COMMON  0u
#define W5X_SOCK0   1u
#define W5X_TX0     2u
#define W5X_RX0     3u
#define W5X_SOCK1   5u          /* the DHCP client's UDP socket, beside socket 0 */
#define W5X_TX1     6u
#define W5X_RX1     7u

/* Each of socket 0's and socket 1's buffers holds 2 KB on both chips: the
 * reset sizing, which neither chip file changes. */
#define W5X_BUF_SIZE 0x0800u

uint8_t      w5x_rd(uint8_t space, uint16_t off);
void         w5x_wr(uint8_t space, uint16_t off, uint8_t v);

/* Resets the chip and checks it is the chip this file drives.
 * TFW_ERR_UNSUPPORTED if it is absent or is the other one. */
tfw_status_t w5x_chip_init(void);

/* For the chip files: resolves the chip select into *cs and parks the shield's
 * SD card off the bus. TFW_ERR_UNSUPPORTED if the board has no such pins. */
tfw_status_t w5x_bus_init(tfw_pinref_t *cs);

/* For the chip files: one transaction of len bytes, in place, in SPI mode 0
 * MSB first. Re-applied every time, because a serial host's SPI_CONFIG may
 * have changed the mode since. */
void         w5x_xfer(const tfw_pinref_t *cs, uint8_t *buf, uint8_t len);

/* Common registers. */
#define W5X_MR      0x0000u
#define W5X_GAR     0x0001u
#define W5X_SUBR    0x0005u
#define W5X_SHAR    0x0009u
#define W5X_SIPR    0x000Fu

#define W5X_MR_RST  0x80u

/* Socket registers. */
#define W5X_SN_MR      0x0000u
#define W5X_SN_CR      0x0001u
#define W5X_SN_IR      0x0002u
#define W5X_SN_SR      0x0003u
#define W5X_SN_PORT    0x0004u
#define W5X_SN_DIPR    0x000Cu
#define W5X_SN_DPORT   0x0010u
#define W5X_SN_TX_FSR  0x0020u
#define W5X_SN_TX_WR   0x0024u
#define W5X_SN_RX_RSR  0x0026u
#define W5X_SN_RX_RD   0x0028u

#define W5X_SN_MR_TCP     0x01u
#define W5X_SN_MR_UDP     0x02u

#define W5X_CR_OPEN       0x01u
#define W5X_CR_CONNECT    0x04u
#define W5X_CR_DISCON     0x08u
#define W5X_CR_CLOSE      0x10u
#define W5X_CR_SEND       0x20u
#define W5X_CR_RECV       0x40u

#define W5X_IR_SEND_OK    0x10u
#define W5X_IR_TIMEOUT    0x08u
#define W5X_IR_DISCON     0x02u

#define W5X_SR_CLOSED      0x00u
#define W5X_SR_INIT        0x13u
#define W5X_SR_SYNSENT     0x15u
#define W5X_SR_UDP         0x22u
#define W5X_SR_ESTABLISHED 0x17u
#define W5X_SR_CLOSE_WAIT  0x1Cu

/* How many times a command or a reset is polled before it is given up on.
 * Each poll is one four-byte SPI transaction, so at the 4 MHz the chip files
 * use this bounds a wait to about two milliseconds. */
#define W5X_POLL_LIMIT  200u

#endif /* TFW_W5X00_H */
