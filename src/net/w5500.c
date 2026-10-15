/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The WIZnet W5500 (Arduino Ethernet Shield 2): its SPI framing and its
 * identity. Linked when the board's ETH_CHIP is w5500.
 *
 * A frame is a 16-bit offset, a control byte -- block select, read or write,
 * variable-length mode -- and the data. The block selects are w5x00.h's
 * register spaces, and the chip wraps a buffer offset into the buffer itself.
 */

#include "w5x00.h"

#define CTRL_WRITE  0x04u
#define VERSIONR    0x0039u
#define VERSION     0x04u

static tfw_pinref_t cs;

static void frame(uint8_t *b, uint8_t space, uint16_t off, uint8_t write, uint8_t v)
{
    b[0] = (uint8_t)(off >> 8);
    b[1] = (uint8_t)off;
    b[2] = (uint8_t)((uint8_t)(space << 3) | (write ? CTRL_WRITE : 0u));
    b[3] = v;
    w5x_xfer(&cs, b, 4u);
}

uint8_t w5x_rd(uint8_t space, uint16_t off)
{
    uint8_t b[4];

    frame(b, space, off, 0u, 0u);
    return b[3];
}

void w5x_wr(uint8_t space, uint16_t off, uint8_t v)
{
    uint8_t b[4];

    frame(b, space, off, 1u, v);
}

/* VERSIONR reads 0x04 on a W5500 and on nothing else. A W5100 takes this
 * framing's leading 0x00 as an opcode it does not have and ignores the frame,
 * reset write included, so the wrong shield fails closed and is not touched. */
tfw_status_t w5x_chip_init(void)
{
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
    if (n == W5X_POLL_LIMIT || w5x_rd(W5X_COMMON, VERSIONR) != VERSION) {
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_OK;
}
