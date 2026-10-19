/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link, over TCP.
 *
 * tfw_hal_uart_* is the HAL's byte link, not a promise of a UART: core's
 * transport (src/proto/transport.c) reads and writes bytes through it and
 * frames them itself -- SOF, length, CRC-16, resync. On this port those bytes
 * travel in one TCP stream on TFW_ESP_WIRE_PORT, unchanged. No second framing
 * and no change to the transport: a frame on the wire is the frame a UART
 * would carry, so the same host codec reads both.
 *
 * One host at a time, and a host in a session keeps it. A connection that
 * arrives while the current host has moved a byte in the last
 * TFW_ESP_WIRE_IDLE_MS, or was taken in that time, is closed at once: it is
 * not the host the device is talking to, and taking it in that one's place
 * would cut a session off between a request and its reply -- an OTA stream
 * ended 3 s in that way, by a host's own abandoned connect that the network
 * completed late (#48). A connection that arrives once the current host has
 * gone quiet replaces it, because the usual reason for one is a host
 * that restarted and left a half-open socket behind; refusing it would lock
 * the device out until TCP gave up on the dead one. A frame cut off by the
 * change is the case the framer's resync already handles.
 *
 * Every path that closes a socket says why, at WARN. A refusal on the wire is
 * a status reply; a close is never one, and a close that leaves no trace is a
 * failure nobody can find.
 *
 * Never blocks, as the contract says. Every socket is non-blocking; bytes
 * written while no host is connected are dropped, exactly as a UART with
 * nothing on its TX pin drops them.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"

#include "tfw/hal.h"
#include "hal_esp32.h"

#define RX_BUF 256u
#define TX_BUF 512u

static const char *TAG = "tfw-wire";

static int     listener = -1;
static int     peer     = -1;
static tfw_ms_t peer_heard;     /* peer's accept, or the last byte moved on it */
static uint8_t rx_buf[RX_BUF];
static uint16_t rx_head;
static uint16_t rx_len;
static uint8_t tx_buf[TX_BUF];
static uint16_t tx_len;

static void nonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void heard(void)
{
    peer_heard = tfw_hal_millis();
}

static void drop_peer(void)
{
    if (peer >= 0) {
        (void)close(peer);
    }
    peer    = -1;
    rx_head = 0u;
    rx_len  = 0u;
    tx_len  = 0u;
}

/* Takes a waiting connection, if there is one: in place of the current one if
 * that has gone quiet, else not at all. A connection is heard when it is taken,
 * so one that has said nothing yet keeps the wire for the window too. */
static void accept_peer(void)
{
    int fd;
    int one = 1;

    if (listener < 0) {
        return;
    }
    fd = accept(listener, NULL, NULL);
    if (fd < 0) {
        return;
    }
    if (peer >= 0) {
        tfw_ms_t quiet = (tfw_ms_t)(tfw_hal_millis() - peer_heard);

        if (quiet < TFW_ESP_WIRE_IDLE_MS) {
            ESP_LOGW(TAG, "refused a second host: the current one moved a byte %u ms ago",
                     (unsigned)quiet);
            (void)close(fd);
            return;
        }
        ESP_LOGW(TAG, "a new host replaces one quiet for %u ms", (unsigned)quiet);
        drop_peer();
    }
    nonblocking(fd);
    /* A response is one small frame the host is waiting on: send it now. */
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    peer = fd;
    heard();
}

static void flush_tx(void)
{
    ssize_t n;

    if (peer < 0 || tx_len == 0u) {
        return;
    }
    n = send(peer, tx_buf, tx_len, MSG_DONTWAIT);
    if (n > 0) {
        memmove(tx_buf, tx_buf + n, (size_t)(tx_len - (uint16_t)n));
        tx_len = (uint16_t)(tx_len - (uint16_t)n);
        heard();
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        int err = errno;

        ESP_LOGW(TAG, "dropped the host: send failed, errno %d", err);
        drop_peer();
    }
}

/* `baud` has no meaning on a TCP stream and is ignored. */
tfw_status_t tfw_hal_uart_init(uint32_t baud)
{
    struct sockaddr_in addr;
    int one = 1;
    tfw_status_t st;

    (void)baud;
    st = tfw_esp_eth_start();
    if (st != TFW_OK) {
        return st;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        return TFW_ERR_INTERNAL;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(TFW_ESP_WIRE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(listener, 1) != 0) {
        int err = errno;

        ESP_LOGW(TAG, "no wire: tcp/%u would not bind or listen, errno %d",
                 (unsigned)TFW_ESP_WIRE_PORT, err);
        (void)close(listener);
        listener = -1;
        return TFW_ERR_INTERNAL;
    }
    nonblocking(listener);
    return TFW_OK;
}

uint8_t tfw_hal_uart_rx(uint8_t *out)
{
    /* The reply first: a command that ran past TFW_ESP_WIRE_IDLE_MS has its
     * host waiting on these bytes, and sending them is what makes it heard. */
    flush_tx();
    accept_peer();
    if (peer < 0) {
        return 0u;
    }
    if (rx_len == 0u) {
        ssize_t n = recv(peer, rx_buf, sizeof rx_buf, MSG_DONTWAIT);
        if (n == 0) {
            ESP_LOGW(TAG, "the host closed the connection");
            drop_peer();
            return 0u;
        }
        if (n < 0) {
            int err = errno;

            if (err != EAGAIN && err != EWOULDBLOCK) {
                ESP_LOGW(TAG, "dropped the host: recv failed, errno %d", err);
                drop_peer();
            }
            return 0u;
        }
        rx_head = 0u;
        rx_len  = (uint16_t)n;
        heard();
    }
    *out = rx_buf[rx_head++];
    rx_len--;
    return 1u;
}

uint8_t tfw_hal_uart_tx_try(uint8_t byte)
{
    if (peer < 0) {
        return 1u;              /* nobody listening: the byte goes nowhere */
    }
    if (tx_len == TX_BUF) {
        flush_tx();
        if (peer < 0) {
            return 1u;          /* the send failed and dropped the host */
        }
        if (tx_len == TX_BUF) {
            return 0u;
        }
    }
    tx_buf[tx_len++] = byte;
    return 1u;
}

uint8_t tfw_hal_uart_tx_idle(void)
{
    flush_tx();
    return (uint8_t)(tx_len == 0u);
}
