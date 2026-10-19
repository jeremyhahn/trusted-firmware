/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * lwIP's socket calls, as src/hal/esp32/hal_uart.c makes them, over an
 * in-memory model (test/mock/esp32/lwip_mock.h): one listener, and up to
 * MOCK_LWIP_CONNS connections a suite opens, feeds and hangs up. Every call is
 * the non-blocking form hal_uart.c uses, so a call with nothing to do fails
 * with EWOULDBLOCK, as lwIP's does. Also esp_log.h's stand-in, which keeps the
 * WARN lines.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip_mock.h"

#define LISTENER_FD 3
#define CONN_FD0    10

mock_lwip_t mock_lwip;

void mock_lwip_reset(void)
{
    memset(&mock_lwip, 0, sizeof mock_lwip);
}

mock_conn_t *mock_lwip_connect(void)
{
    mock_conn_t *c;

    if (mock_lwip.nconn == MOCK_LWIP_CONNS) {
        return NULL;
    }
    c = &mock_lwip.conn[mock_lwip.nconn++];
    c->pending = 1u;
    return c;
}

void mock_lwip_feed(mock_conn_t *c, const uint8_t *b, uint16_t n)
{
    /* What the device has read is gone; what it has not moves to the front. */
    memmove(c->in, &c->in[c->in_pos], (size_t)(c->in_len - c->in_pos));
    c->in_len = (uint16_t)(c->in_len - c->in_pos);
    c->in_pos = 0u;
    if (n > MOCK_LWIP_BUF - c->in_len) {
        n = (uint16_t)(MOCK_LWIP_BUF - c->in_len);
    }
    memcpy(&c->in[c->in_len], b, n);
    c->in_len = (uint16_t)(c->in_len + n);
}

static mock_conn_t *conn_of(int s)
{
    if (s < CONN_FD0 || s >= CONN_FD0 + (int)mock_lwip.nconn) {
        return NULL;
    }
    return &mock_lwip.conn[s - CONN_FD0];
}

int lwip_socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    return LISTENER_FD;
}

int lwip_bind(int s, const struct sockaddr *name, socklen_t namelen)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)name;

    if (s != LISTENER_FD || namelen != sizeof *in || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    if (mock_lwip.bind_errno != 0) {
        errno = mock_lwip.bind_errno;
        return -1;
    }
    mock_lwip.port = lwip_htons(in->sin_port);
    return 0;
}

int lwip_listen(int s, int backlog)
{
    (void)backlog;
    if (s != LISTENER_FD) {
        errno = EBADF;
        return -1;
    }
    mock_lwip.listening = 1u;
    return 0;
}

int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    unsigned i;

    (void)addr; (void)addrlen;
    if (s != LISTENER_FD || !mock_lwip.listening) {
        errno = EBADF;
        return -1;
    }
    for (i = 0u; i < mock_lwip.nconn; i++) {
        if (mock_lwip.conn[i].pending) {
            mock_lwip.conn[i].pending = 0u;
            mock_lwip.conn[i].open    = 1u;
            mock_lwip.accepts++;
            return CONN_FD0 + (int)i;
        }
    }
    errno = EWOULDBLOCK;
    return -1;
}

int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    mock_conn_t *c = conn_of(s);

    (void)optlen;
    if (c != NULL && level == IPPROTO_TCP && optname == TCP_NODELAY) {
        c->nodelay = (uint8_t)(*(const int *)optval != 0);
    }
    return 0;
}

ssize_t lwip_recv(int s, void *mem, size_t len, int flags)
{
    mock_conn_t *c = conn_of(s);
    size_t       n;

    if (c == NULL || !c->open || !(flags & MSG_DONTWAIT)) {
        errno = EBADF;
        return -1;
    }
    if (c->in_pos < c->in_len) {
        n = (size_t)(c->in_len - c->in_pos);
        n = n < len ? n : len;
        memcpy(mem, &c->in[c->in_pos], n);
        c->in_pos = (uint16_t)(c->in_pos + n);
        return (ssize_t)n;
    }
    if (c->hung_up) {
        return 0;
    }
    errno = c->recv_errno != 0 ? c->recv_errno : EWOULDBLOCK;
    return -1;
}

ssize_t lwip_send(int s, const void *data, size_t size, int flags)
{
    mock_conn_t *c = conn_of(s);
    size_t       n;

    if (c == NULL || !c->open || !(flags & MSG_DONTWAIT)) {
        errno = EBADF;
        return -1;
    }
    if (c->send_errno != 0) {
        errno = c->send_errno;
        return -1;
    }
    n = MOCK_LWIP_BUF - c->out_len;
    if (n == 0u) {
        errno = EWOULDBLOCK;
        return -1;
    }
    n = n < size ? n : size;
    memcpy(&c->out[c->out_len], data, n);
    c->out_len = (uint16_t)(c->out_len + n);
    return (ssize_t)n;
}

int lwip_close(int s)
{
    mock_conn_t *c = conn_of(s);

    if (s == LISTENER_FD) {
        mock_lwip.listening = 0u;
    } else if (c != NULL && c->open) {
        c->open   = 0u;
        c->closed = 1u;
    } else {
        errno = EBADF;
        return -1;
    }
    mock_lwip.closes++;
    return 0;
}

int lwip_fcntl(int s, int cmd, int val)
{
    mock_conn_t *c   = conn_of(s);
    uint8_t     *flg = s == LISTENER_FD ? &mock_lwip.listen_nonblock : c != NULL ? &c->nonblock : NULL;

    if (flg == NULL) {
        errno = EBADF;
        return -1;
    }
    if (cmd == F_GETFL) {
        return *flg ? O_NONBLOCK : 0;
    }
    if (cmd == F_SETFL) {
        *flg = (uint8_t)((val & O_NONBLOCK) != 0);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

uint16_t lwip_htons(uint16_t n)
{
    return (uint16_t)((n >> 8) | (n << 8));
}

uint32_t lwip_htonl(uint32_t n)
{
    return (n >> 24) | ((n >> 8) & 0xFF00u) | ((n << 8) & 0xFF0000u) | (n << 24);
}

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    int     n;

    if (level != 'W') {
        return;
    }
    n = snprintf(mock_lwip.last_warn, sizeof mock_lwip.last_warn, "%s: ", tag);
    va_start(ap, fmt);
    (void)vsnprintf(&mock_lwip.last_warn[n], sizeof mock_lwip.last_warn - (size_t)n, fmt, ap);
    va_end(ap);
    mock_lwip.warns++;
}
