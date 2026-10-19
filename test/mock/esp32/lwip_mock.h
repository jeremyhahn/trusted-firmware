/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The handle a suite holds on test/mock/lwip_mock.c: one listening socket and
 * the connections hosts make to it, each with what the host sent, what the
 * device sent back, and whether either side has closed it. Also the WARN lines
 * esp_log.h's stand-in recorded.
 */
#ifndef TFW_MOCK_LWIP_MOCK_H
#define TFW_MOCK_LWIP_MOCK_H

#include <stdint.h>

#define MOCK_LWIP_CONNS 8u
#define MOCK_LWIP_BUF   1024u

typedef struct {
    uint8_t  pending;        /* connected, not yet accepted */
    uint8_t  open;           /* accepted, and the device has not closed it */
    uint8_t  closed;         /* the device closed it */
    uint8_t  hung_up;        /* the host closed: recv reads 0 once `in` is drained */
    uint8_t  nonblock;
    uint8_t  nodelay;
    int      recv_errno;     /* nonzero: recv fails with it once `in` is drained */
    int      send_errno;     /* nonzero: send fails with it */
    uint8_t  in[MOCK_LWIP_BUF];
    uint16_t in_len, in_pos;
    uint8_t  out[MOCK_LWIP_BUF];
    uint16_t out_len;
} mock_conn_t;

typedef struct {
    uint8_t     listening;
    uint8_t     listen_nonblock;
    uint16_t    port;            /* host order */
    int         bind_errno;      /* nonzero: bind fails with it */
    mock_conn_t conn[MOCK_LWIP_CONNS];
    unsigned    nconn;
    unsigned    accepts;
    unsigned    closes;          /* by the device, listener included */
    unsigned    warns;
    char        last_warn[160];
} mock_lwip_t;

extern mock_lwip_t mock_lwip;

/* Forgets every socket and every logged line. */
void mock_lwip_reset(void);

/* A host connects; returns its connection, pending until the device accepts. */
mock_conn_t *mock_lwip_connect(void);

/* The host sends n bytes on c. */
void mock_lwip_feed(mock_conn_t *c, const uint8_t *b, uint16_t n);

#endif
