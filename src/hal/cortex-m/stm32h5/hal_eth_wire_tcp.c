/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tfw-wire TCP connection on the STM32H5's MAC (#582, #584): lwIP's raw
 * API under NO_SYS, polled from the main loop with the interface
 * (hal_eth_netif.c). The device opens the one connection to the endpoint in
 * its network block and listens on nothing (docs/SPEC.md, ethernet-transport;
 * the H5 Ethernet ADR, "How the frames ride TCP"). A refused or dropped
 * connection is dialled again after a backoff, the block read afresh each
 * time (never cached, include/tfw/eth.h). The link logic is hal_eth_wire.c;
 * this file only moves bytes between it and lwIP.
 */
#include <stdint.h>

#include "tfw/eth.h"
#include "tfw/hal.h"
#include "hal_eth_wire.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

static struct tcp_pcb *wire_pcb;        /* the open connection */
static struct tcp_pcb *wire_dial_pcb;   /* a connect not yet answered */
static tfw_ms_t        wire_dial_ms;    /* when the wait began */
static uint32_t        wire_wait_ms;    /* 0: dial at once */

static void wire_backoff(uint32_t wait)
{
    wire_dial_ms = tfw_hal_millis();
    wire_wait_ms = wait;
}

/* A dial refused: the wait doubles, up to TFW_H5_WIRE_REDIAL_MAX_MS. */
static void wire_refused(void)
{
    wire_dial_pcb = NULL;
    wire_backoff(wire_wait_ms >= TFW_H5_WIRE_REDIAL_MAX_MS / 2u ?
                 TFW_H5_WIRE_REDIAL_MAX_MS : wire_wait_ms * 2u);
}

static void wire_forget(void)
{
    if (wire_pcb != NULL) {
        tcp_arg(wire_pcb, NULL);
        tcp_recv(wire_pcb, NULL);
        tcp_err(wire_pcb, NULL);
        wire_pcb = NULL;
        wire_backoff(TFW_H5_WIRE_REDIAL_MS);
    }
    tfw_h5_wire_drop();
}

static void wire_err(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    /* lwIP has freed the pcb: a refused connect, or the open one reset. */
    if (wire_dial_pcb != NULL) {
        wire_refused();
        return;
    }
    wire_pcb = NULL;
    wire_backoff(TFW_H5_WIRE_REDIAL_MS);
    tfw_h5_wire_drop();
}

static err_t wire_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    /* FIN, perhaps in the segment that carried the request: lwIP hands the
     * data over first, then p == NULL. The request is still answered; the
     * link closes the connection once it is (tfw_h5_wire_finish). */
    if (p == NULL && err == ERR_OK && pcb == wire_pcb) {
        tfw_h5_wire_eof();
        return ERR_OK;
    }
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

static err_t wire_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (pcb != wire_dial_pcb) {
        return ERR_VAL;
    }
    wire_dial_pcb = NULL;
    if (err != ERR_OK) {
        tcp_err(pcb, NULL);
        tcp_abort(pcb);
        wire_refused();
        return ERR_ABRT;
    }
    wire_pcb = pcb;
    wire_wait_ms = 0u;
    tcp_recv(pcb, wire_recv);
    tcp_nagle_disable(pcb);
    /* A peer that loses its state without FIN or RST would hold the wire
     * forever: unanswered keepalives make lwIP abort the pcb (wire_err),
     * and the poll dials again. */
    ip_set_option(pcb, SOF_KEEPALIVE);
    pcb->keep_idle  = TFW_H5_WIRE_KEEP_IDLE_MS;
    pcb->keep_intvl = TFW_H5_WIRE_KEEP_INTVL_MS;
    pcb->keep_cnt   = TFW_H5_WIRE_KEEP_CNT;
    tfw_h5_wire_open();
    return ERR_OK;
}

void tfw_h5_wire_dial_poll(void)
{
    tfw_netcfg_t cfg;
    ip_addr_t peer;
    struct tcp_pcb *pcb;

    if (wire_pcb != NULL || wire_dial_pcb != NULL) {
        return;
    }
    if (wire_wait_ms != 0u &&
        (tfw_ms_t)(tfw_hal_millis() - wire_dial_ms) < wire_wait_ms) {
        return;
    }
    wire_dial_ms = tfw_hal_millis();        /* the next try waits at least */
    if (wire_wait_ms == 0u) {
        wire_wait_ms = TFW_H5_WIRE_REDIAL_MS;
    }
    if (tfw_netcfg_load(&cfg) != TFW_OK) {
        return;                         /* not provisioned: nothing to dial */
    }
    /* The block may have been rewritten over serial since bring-up: bring
     * the netif to it first, and dial only once it is up on it. */
    if (!tfw_h5_eth_reconcile(&cfg)) {
        return;
    }
    pcb = tcp_new();
    if (pcb == NULL) {
        return;
    }
    IP_ADDR4(&peer, cfg.peer[0], cfg.peer[1], cfg.peer[2], cfg.peer[3]);
    tcp_arg(pcb, NULL);
    tcp_err(pcb, wire_err);
    wire_dial_pcb = pcb;
    if (tcp_connect(pcb, &peer, cfg.port, wire_connected) != ERR_OK) {
        tcp_err(pcb, NULL);
        wire_refused();
        tcp_abort(pcb);
    }
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

void tfw_h5_wire_abort(void)
{
    struct tcp_pcb *pcb = wire_pcb;

    wire_forget();                      /* the link drops the peer at once */
    if (pcb != NULL) {
        tcp_abort(pcb);                 /* RST: the peer is not reading */
    }
}

void tfw_h5_wire_finish(void)
{
    struct tcp_pcb *pcb = wire_pcb;

    if (pcb == NULL) {
        return;
    }
    if (tcp_sndqueuelen(pcb) != 0u) {
        (void)tcp_output(pcb);
        return;                         /* the reply is still in flight */
    }
    wire_forget();
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);                 /* no memory for the FIN: reset */
    }
}
