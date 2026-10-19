/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for lwIP's sockets.h as ESP-IDF v5.5.5 ships it: the calls,
 * types and constants src/hal/esp32/hal_uart.c names, with lwIP's values
 * (src/include/lwip/sockets.h). As lwIP's LWIP_COMPAT_SOCKETS does, each call
 * is a macro onto its lwip_ name; close and fcntl, which ESP-IDF routes to lwIP
 * through its VFS, go the same way. test/mock/lwip_mock.c implements them.
 */
#ifndef TFW_MOCK_LWIP_SOCKETS_H
#define TFW_MOCK_LWIP_SOCKETS_H

#include <stdint.h>
#include <sys/types.h>

typedef uint8_t  sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;
typedef uint32_t socklen_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    uint8_t     sa_len;
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_in {
    uint8_t        sin_len;
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

#define AF_INET      2
#define SOCK_STREAM  1
#define IPPROTO_TCP  6
#define SOL_SOCKET   0xfff
#define SO_REUSEADDR 0x0004
#define TCP_NODELAY  0x01
#define MSG_DONTWAIT 0x08
#define INADDR_ANY   ((in_addr_t)0x00000000UL)

int      lwip_socket(int domain, int type, int protocol);
int      lwip_bind(int s, const struct sockaddr *name, socklen_t namelen);
int      lwip_listen(int s, int backlog);
int      lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int      lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
ssize_t  lwip_recv(int s, void *mem, size_t len, int flags);
ssize_t  lwip_send(int s, const void *data, size_t size, int flags);
int      lwip_close(int s);
int      lwip_fcntl(int s, int cmd, int val);
uint16_t lwip_htons(uint16_t n);
uint32_t lwip_htonl(uint32_t n);

#define socket(d, t, p)               lwip_socket(d, t, p)
#define bind(s, n, l)                 lwip_bind(s, n, l)
#define listen(s, b)                  lwip_listen(s, b)
#define accept(s, a, l)               lwip_accept(s, a, l)
#define setsockopt(s, lv, o, v, l)    lwip_setsockopt(s, lv, o, v, l)
#define recv(s, m, l, f)              lwip_recv(s, m, l, f)
#define send(s, d, n, f)              lwip_send(s, d, n, f)
#define close(s)                      lwip_close(s)
#define fcntl(s, c, v)                lwip_fcntl(s, c, v)
#define htons(n)                      lwip_htons(n)
#define htonl(n)                      lwip_htonl(n)

#endif
