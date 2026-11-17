/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tfw-wire TCP listener on the STM32H5's MAC (#582): lwIP's raw API under
 * NO_SYS, polled from the main loop with the interface (hal_eth_netif.c). One
 * connection at a time; a new one replaces the one held, so a host that went
 * away without closing never locks the board out. The link logic is
 * hal_eth_wire.c; this file only moves bytes between it and lwIP.
 */
#include <stdint.h>

#include "hal_eth_wire.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"

static struct tcp_pcb *wire_listen_pcb;
static struct tcp_pcb *wire_pcb;

static void wire_forget(void)
{
    if (wire_pcb != NULL) {
        tcp_arg(wire_pcb, NULL);
        tcp_recv(wire_pcb, NULL);
        tcp_err(wire_pcb, NULL);
        wire_pcb = NULL;
    }
    tfw_h5_wire_drop();
}

static void wire_err(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    wire_pcb = NULL;                    /* lwIP has freed it */
    tfw_h5_wire_drop();
}

static err_t wire_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (p == NULL || err != ERR_OK) {
        if (p != NULL) {
            pbuf_free(p);
        }
        if (pcb == wire_pcb) {
            wire_forget();
        }
        (void)tcp_close(pcb);
        return ERR_OK;
    }
    if (pcb != wire_pcb) {
        pbuf_free(p);
        return ERR_OK;
    }
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (tfw_h5_wire_push((const uint8_t *)q->payload, q->len) != q->len) {
            break;                      /* window overrun: the rest is lost */
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t wire_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || pcb == NULL) {
        return ERR_VAL;
    }
    if (wire_pcb != NULL) {
        struct tcp_pcb *old = wire_pcb;
        wire_forget();
        tcp_abort(old);
    }
    wire_pcb = pcb;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, wire_recv);
    tcp_err(pcb, wire_err);
    tcp_nagle_disable(pcb);
    tfw_h5_wire_open();
    return ERR_OK;
}

void tfw_h5_wire_listen(void)
{
    struct tcp_pcb *pcb;

    if (wire_listen_pcb != NULL) {
        return;
    }
    pcb = tcp_new();
    if (pcb == NULL) {
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, TFW_H5_WIRE_PORT) != ERR_OK) {
        (void)tcp_close(pcb);
        return;
    }
    wire_listen_pcb = tcp_listen_with_backlog(pcb, 1);
    if (wire_listen_pcb == NULL) {
        (void)tcp_close(pcb);
        return;
    }
    tcp_accept(wire_listen_pcb, wire_accept);
}

uint16_t tfw_h5_wire_send(const uint8_t *buf, uint16_t len)
{
    uint16_t room;

    if (wire_pcb == NULL) {
        return len;                     /* gone: the bytes are dropped */
    }
    room = (uint16_t)tcp_sndbuf(wire_pcb);
    if (room == 0u || tcp_sndqueuelen(wire_pcb) >= TCP_SND_QUEUELEN) {
        (void)tcp_output(wire_pcb);
        return 0u;
    }
    if (len > room) {
        len = room;
    }
    if (tcp_write(wire_pcb, buf, len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        (void)tcp_output(wire_pcb);
        return 0u;
    }
    (void)tcp_output(wire_pcb);
    return len;
}

void tfw_h5_wire_recved(uint16_t n)
{
    if (wire_pcb != NULL) {
        tcp_recved(wire_pcb, n);
    }
}
