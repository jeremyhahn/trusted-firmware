/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The WIZnet W5100 (Arduino Ethernet Shield Rev3): its SPI framing and its
 * identity. Linked when the board's ETH_CHIP is w5100.
 *
 * A frame is four bytes: 0xF0 to write or 0x0F to read, a 16-bit address, one
 * data byte. The address map is flat: common registers at 0, socket 0 at
 * 0x0400, its 2 KB transmit buffer at 0x4000 and receive buffer at 0x6000.
 * Socket 1 follows each: 0x0500, 0x4800, 0x6800.
 */

#include "w5x00.h"

#define OP_WRITE    0xF0u
#define OP_READ     0x0Fu

#define SOCK0_BASE  0x0400u
#define SOCK_STEP   0x0100u       /* socket n's registers at 0x0400 + n * 0x100 */
#define TX0_BASE    0x4000u
#define RX0_BASE    0x6000u
#define BUF_MASK    (W5X_BUF_SIZE - 1u)

#define RTR_HI      0x0017u       /* retry time, 0x07D0 (200 ms) after reset */
#define RTR_LO      0x0018u
#define RCR         0x0019u       /* retry count, 8 after reset */

static tfw_pinref_t cs;

static uint16_t map(uint8_t space, uint16_t off)
{
    switch (space) {
    case W5X_SOCK0: return (uint16_t)(SOCK0_BASE + off);
    case W5X_TX0:   return (uint16_t)(TX0_BASE + (off & BUF_MASK));
    case W5X_RX0:   return (uint16_t)(RX0_BASE + (off & BUF_MASK));
    case W5X_SOCK1: return (uint16_t)(SOCK0_BASE + SOCK_STEP + off);
    case W5X_TX1:   return (uint16_t)(TX0_BASE + W5X_BUF_SIZE + (off & BUF_MASK));
    case W5X_RX1:   return (uint16_t)(RX0_BASE + W5X_BUF_SIZE + (off & BUF_MASK));
    default:        return off;
    }
}

static void frame(uint8_t *b, uint8_t op, uint16_t addr, uint8_t v)
{
    b[0] = op;
    b[1] = (uint8_t)(addr >> 8);
    b[2] = (uint8_t)addr;
    b[3] = v;
    w5x_xfer(&cs, b, 4u);
}

uint8_t w5x_rd(uint8_t space, uint16_t off)
{
    uint8_t b[4];

    frame(b, OP_READ, map(space, off), 0u);
    return b[3];
}

void w5x_wr(uint8_t space, uint16_t off, uint8_t v)
{
    uint8_t b[4];

    frame(b, OP_WRITE, map(space, off), v);
}

/* The W5100 has no version register; it is known by what it does. It shifts
 * out 0x00, 0x01, 0x02 during every frame's header, and after a mode-register
 * reset its retry time reads 0x07D0 and its retry count 0x08 -- the pattern
 * read off the reference Mega's shield (docs/development/wiznet-probe.md).
 * The header is checked first, on a read of RTR's low byte, which a W5500
 * takes as a read of an unrelated block. Only once the echo has ruled a W5500
 * out is 0x0017 read: in W5500 framing that frame is a write. An empty socket
 * reads all-ones or all-zeros. The reset write is harmless to a W5500 too --
 * it reads 0xF0 as an address byte -- so the wrong shield fails closed. */
tfw_status_t w5x_chip_init(void)
{
    uint8_t b[4];
    uint8_t n;

    if (w5x_bus_init(&cs) != TFW_OK) {
        return TFW_ERR_UNSUPPORTED;
    }
    w5x_wr(W5X_COMMON, W5X_MR, W5X_MR_RST);
    for (n = 0u; n < W5X_POLL_LIMIT; n++) {
        if ((w5x_rd(W5X_COMMON, W5X_MR) & W5X_MR_RST) == 0u) {
            break;
        }
    }
    frame(b, OP_READ, RTR_LO, 0u);
    if (n == W5X_POLL_LIMIT ||
        b[0] != 0x00u || b[1] != 0x01u || b[2] != 0x02u || b[3] != 0xD0u ||
        w5x_rd(W5X_COMMON, RCR) != 0x08u ||
        w5x_rd(W5X_COMMON, RTR_HI) != 0x07u) {
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_OK;
}
