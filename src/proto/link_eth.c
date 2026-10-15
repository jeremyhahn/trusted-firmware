/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The link for an Ethernet image: the UART and one TCP connection the device
 * opens to a provisioned endpoint, both feeding the one transport.
 *
 *   Binding.   A frame belongs to the wire that delivered its first byte, and
 *              its reply goes back on that wire. While a frame is arriving the
 *              other wire is not read; its bytes wait in the UART ring or in
 *              the controller's socket buffer. Between frames the wire that did
 *              not carry the last one is read first, so neither starves.
 *   Events.    Serial only. They are for the bench host that owns the pins.
 *   Allow-list Ethernet runs the attestation and enrolment commands and nothing
 *              else. Everything else is answered as a command the image does
 *              not have -- which, from the network, it does not.
 *   Reconnect. On a deadline in tfw_hal_millis() time, never a delay: 1 s after
 *              a failure, doubling to 64 s, back to 1 s once connected. A blank
 *              network block, an absent controller or the wrong one keeps the
 *              Ethernet side down and leaves serial exactly as it was.
 *
 * docs/ethernet-transport/ has the reasons.
 */

#include "tfw/link.h"
#include "tfw/eth.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define WIRE_SERIAL 0u
#define WIRE_ETH    1u

#define BACKOFF_BASE_MS 1000u
#define BACKOFF_MAX     6u          /* 1 s << 6 = 64 s */
#define PORT_EPHEMERAL  49152u

static uint8_t  phase;              /* TFW_ETH_DOWN / BUSY / UP */
static uint8_t  backoff;
static tfw_ms_t next_ms;            /* earliest next attempt while DOWN */
static uint8_t  cur;                /* the wire of the current or last frame */

static void retry_later(tfw_ms_t now)
{
    phase   = TFW_ETH_DOWN;
    next_ms = now + ((tfw_ms_t)BACKOFF_BASE_MS << backoff);
    if (backoff < BACKOFF_MAX) {
        backoff++;
    }
}

void tfw_link_poll(void)
{
    tfw_ms_t     now = tfw_hal_millis();
    tfw_netcfg_t cfg;
    uint8_t      st;

    if (phase == TFW_ETH_DOWN) {
        if ((int32_t)(now - next_ms) < 0) {
            return;
        }
        /* A fresh source port each attempt, so the endpoint's TCP never takes
         * a reconnect for the connection it replaces. */
        if (tfw_netcfg_load(&cfg) == TFW_OK &&
            tfw_eth_start(&cfg, (uint16_t)(PORT_EPHEMERAL + (now & 0x3FFFu))) == TFW_OK) {
            phase = TFW_ETH_BUSY;
        } else {
            retry_later(now);
        }
        return;
    }

    st = tfw_eth_state();
    if (st == TFW_ETH_UP) {
        phase   = TFW_ETH_UP;
        backoff = 0u;
    } else if (st == TFW_ETH_DOWN) {
        tfw_eth_close();
        retry_later(now);
    }
}

static uint8_t eth_rx(uint8_t *out)
{
    return (phase == TFW_ETH_UP) ? tfw_eth_rx(out) : 0u;
}

uint8_t tfw_link_rx(uint8_t *out, uint8_t hold)
{
    if (hold) {
        return (cur == WIRE_ETH) ? eth_rx(out) : tfw_hal_uart_rx(out);
    }
    if (cur == WIRE_SERIAL) {
        if (eth_rx(out)) {
            cur = WIRE_ETH;
            return 1u;
        }
        return tfw_hal_uart_rx(out);
    }
    if (tfw_hal_uart_rx(out)) {
        cur = WIRE_SERIAL;
        return 1u;
    }
    return eth_rx(out);
}

uint8_t tfw_link_tx_try(uint8_t byte, uint8_t serial)
{
    if (serial || cur == WIRE_SERIAL) {
        return tfw_hal_uart_tx_try(byte);
    }
    /* The connection this reply was owed to has gone: drop it rather than let
     * a dead peer hold the transport and starve serial. */
    if (phase != TFW_ETH_UP) {
        return 1u;
    }
    return tfw_eth_tx(byte);
}

void tfw_link_tx_done(uint8_t serial)
{
    if (!serial && cur == WIRE_ETH && phase == TFW_ETH_UP) {
        tfw_eth_send();
    }
}

/* The Ethernet allow-list. test_link_eth pins it, id by id, against literals:
 * a command added here is a decision about what the network may ask for. */
uint8_t tfw_link_admits(uint8_t cmd)
{
    if (cur == WIRE_SERIAL) {
        return 1u;
    }
    switch (cmd) {
    case TFW_CMD_CAPS:          /* enrolment: what the part can hold */
    case TFW_CMD_ATTEST:        /* UCCS evidence over the verifier's nonce */
    case TFW_CMD_ATTEST_CERT:   /* the alias certificate over the nonce */
    case TFW_CMD_ATTEST_CSR:    /* the templated CSR for ACME finalize */
        return 1u;
    default:
        return 0u;
    }
}
