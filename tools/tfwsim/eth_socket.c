/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * include/tfw/eth.h over a real host TCP socket, so tfwsim is an Ethernet
 * device a host accepts a connection from -- issue #55. It stands where
 * src/net/w5x00.c and a chip file stand in a device image; src/proto/link_eth.c
 * above it is the device's own code, unchanged.
 *
 * Where it differs from the controller, and why the link cannot tell:
 *
 *   Addresses.    The host's stack owns the interface, so the block's MAC, IP,
 *                 mask and gateway are read and not used. Only the endpoint is
 *                 dialled, which is the half a host test is about.
 *   Source port.  The host's choice, not local_port: binding a fixed port on a
 *                 shared machine collides with whatever already holds it.
 *   Transmit.     Queued up to the controller's 2 KB and written in one piece
 *                 by tfw_eth_send(), as the part sends it.
 *
 * Off unless tfwsim is started with --eth. Until then tfw_eth_start() answers
 * as an absent controller does, and a serial-only run is what it always was.
 */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "tfw/eth.h"
#include "eth_socket.h"

#define TXQ_LEN 2048u

static uint8_t enabled;
static int     fd = -1;
static uint8_t state = TFW_ETH_DOWN;
static uint8_t rxb[512];
static size_t  rx_len, rx_pos;
static uint8_t txq[TXQ_LEN];
static size_t  tx_len;

void tfwsim_eth_enable(void)
{
    enabled = 1u;
}

int tfwsim_eth_fd(void)
{
    return fd;
}

static uint8_t would_block(void)
{
    return (uint8_t)(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
}

void tfw_eth_close(void)
{
    if (fd >= 0) {
        (void)close(fd);
    }
    fd     = -1;
    state  = TFW_ETH_DOWN;
    rx_len = rx_pos = tx_len = 0u;
}

tfw_status_t tfw_eth_start(const tfw_netcfg_t *cfg, uint16_t local_port)
{
    struct sockaddr_in to;

    (void)local_port;
    if (!enabled) {
        return TFW_ERR_UNSUPPORTED;
    }
    tfw_eth_close();
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        tfw_eth_close();
        return TFW_ERR_UNSUPPORTED;
    }
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port   = htons(cfg->port);
    memcpy(&to.sin_addr, cfg->peer, 4u);

    if (connect(fd, (const struct sockaddr *)&to, sizeof to) == 0) {
        state = TFW_ETH_UP;
    } else if (errno == EINPROGRESS) {
        state = TFW_ETH_BUSY;
    } else {
        state = TFW_ETH_DOWN;                 /* refused: the link retries */
    }
    return TFW_OK;
}

uint8_t tfw_eth_state(void)
{
    struct pollfd p;
    int           err = 0;
    socklen_t     len = sizeof err;
    uint8_t       b;
    ssize_t       n;

    if (fd < 0) {
        return TFW_ETH_DOWN;
    }
    if (state == TFW_ETH_BUSY) {
        p.fd = fd; p.events = POLLOUT; p.revents = 0;
        if (poll(&p, 1u, 0) > 0) {
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                state = TFW_ETH_UP;
            } else {
                tfw_eth_close();
            }
        }
    } else if (state == TFW_ETH_UP && rx_pos == rx_len) {
        /* The peer's FIN reads as a zero-length receive. Peeked, so a byte
         * that is there instead stays for tfw_eth_rx(). */
        n = recv(fd, &b, 1u, MSG_PEEK);
        if (n == 0 || (n < 0 && !would_block())) {
            tfw_eth_close();
        }
    }
    return state;
}

uint8_t tfw_eth_rx(uint8_t *out)
{
    ssize_t n;

    if (rx_pos == rx_len) {
        if (fd < 0 || state != TFW_ETH_UP) {
            return 0u;
        }
        n = recv(fd, rxb, sizeof rxb, 0);
        if (n <= 0) {
            return 0u;                        /* a close is tfw_eth_state()'s to see */
        }
        rx_len = (size_t)n;
        rx_pos = 0u;
    }
    *out = rxb[rx_pos++];
    return 1u;
}

uint8_t tfw_eth_tx(uint8_t byte)
{
    if (tx_len == TXQ_LEN) {
        return 0u;
    }
    txq[tx_len++] = byte;
    return 1u;
}

/* A peer that stops reading for a second is treated as gone, as the part's
 * own retransmission timeout would. */
void tfw_eth_send(void)
{
    struct pollfd p;
    size_t        off = 0u;
    ssize_t       n;

    while (fd >= 0 && off < tx_len) {
        n = send(fd, &txq[off], tx_len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        p.fd = fd; p.events = POLLOUT; p.revents = 0;
        if (n < 0 && would_block() && poll(&p, 1u, 1000) > 0) {
            continue;
        }
        tfw_eth_close();
    }
    tx_len = 0u;
}
