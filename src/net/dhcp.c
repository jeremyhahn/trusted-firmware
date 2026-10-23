/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DHCP client (RFC 2131, options from RFC 2132) for a network block whose
 * flags ask for DHCP. It runs on the controller's socket 1 in UDP mode, beside
 * the connection on socket 0, hands the link a leased IP, mask and gateway,
 * and keeps the lease while the link is connected from it.
 *
 *   INIT        Opens the socket on port 68 with no address and waits a random
 *               1 to 10 s before the first DHCPDISCOVER (§4.4.1).
 *   SELECTING   Broadcasts DHCPDISCOVER; takes the first DHCPOFFER for its xid.
 *   REQUESTING  Broadcasts DHCPREQUEST naming the offer's server and address
 *               (§4.3.2, Table 5); a DHCPACK probes, a DHCPNAK starts again.
 *   PROBING     Sends one datagram to the address while the controller still
 *               has none (§2.2, §4.4.1). The controller sends it only once a
 *               host answers its ARP for the address; when none does, the
 *               address is free and the client is bound.
 *   DECLINED    The address answered: broadcasts DHCPDECLINE and waits 10 s
 *               before starting again from INIT (§3.1 step 5, §4.4.1).
 *   BOUND       Hands the lease to the link, which connects from it. The
 *               lease counts from the first DHCPREQUEST (§4.4.1).
 *   RENEWING    From T1, half the lease: reopens the socket beside the
 *               connection and sends DHCPREQUEST to the server that granted
 *               the lease, with its address in ciaddr (§4.3.2, §4.4.5).
 *   REBINDING   From T2, seven eighths of the lease: the same, broadcast, so
 *               any server may answer. A DHCPACK for the same address binds
 *               again; a DHCPNAK, or the lease running out, loses it and the
 *               link drops the connection.
 *
 * Retransmission while asking is §4.1's: 4 s, doubling to 64 s, each randomised
 * by +-1 s; after the 64 s wait the attempt has failed and the link backs off.
 * While renewing or rebinding it is §4.4.5's: half the time left to T2 or to
 * the end of the lease, at least 60 s. Until it holds an address the client
 * sets the BROADCAST flag, because the controller cannot take a unicast to an
 * address it has not been given. DHCPDISCOVER and DHCPREQUEST carry the same
 * parameter request list (§4.3.1).
 *
 * T1 and T2 are the §4.4.5 defaults: the server's options 58 and 59 are not
 * read, because a lease start and length are all the RAM the client keeps for
 * them. A renewed lease counts from T1, or from T2 when rebound: no later than
 * the request was sent, as §4.4.5 asks. Not here: DHCPRELEASE and option
 * overload, whose messages are discarded. docs/ethernet-transport/ has the
 * reasons.
 */

#include "tfw/eth.h"
#include "tfw/hal.h"

#define S_INIT        0u
#define S_SELECTING   1u
#define S_REQUESTING  2u
#define S_PROBING     3u
#define S_DECLINED    4u
#define S_BOUND       5u            /* from here on the client holds a lease */
#define S_RENEWING    6u
#define S_REBINDING   7u

#define PORT_DISCARD  9u            /* the probe's: nothing there answers it */
#define PORT_SERVER   67u
#define PORT_CLIENT   68u

#define MSG_DISCOVER  1u
#define MSG_OFFER     2u
#define MSG_REQUEST   3u
#define MSG_DECLINE   4u
#define MSG_ACK       5u
#define MSG_NAK       6u

#define OPT_PAD       0u
#define OPT_MASK      1u
#define OPT_ROUTER    3u
#define OPT_REQ_IP    50u
#define OPT_LEASE     51u
#define OPT_OVERLOAD  52u
#define OPT_TYPE      53u
#define OPT_SERVER    54u
#define OPT_PARAMS    55u
#define OPT_END       255u

#define BOOTP_FIXED   236u          /* op .. file, before the magic cookie */
#define BOOTP_MIN     300u          /* the BOOTP message size relays expect */
#define UDP_HEADER    8u            /* the controller's, before each datagram */

#define WAIT_FIRST_MS 1000u         /* 1 s + up to 9 s before DHCPDISCOVER */
#define WAIT_SPAN_MS  9000u
#define RETX_BASE_MS  4000u
#define RETX_SENDS    5u            /* 4, 8, 16, 32, 64 s */
#define RETX_JITTER   1000u
#define PROBE_MS      4000u         /* the controller's ARP gives up sooner */
#define DECLINE_MS    10000u        /* §3.1 step 5: at least 10 s */
#define RENEW_MIN_MS  60000u        /* §4.4.5: at least 60 s between requests */
#define LEASE_MAX_S   2000000u      /* < 2^31 ms: the deadline compare holds */

/* Reply bytes the client keeps, by what they were seen to carry. */
#define GOT_SERVER    0x01u
#define GOT_LEASE     0x02u
#define GOT_MASK      0x04u
#define GOT_ROUTER    0x08u
#define GOT_END       0x10u

typedef struct {
    uint8_t yiaddr[4];
    uint8_t server[4];
    uint8_t lease[4];
    uint8_t mask[4];
    uint8_t router[4];
    uint8_t type;
    uint8_t got;
} reply_t;

/* No tables: on AVR a const array is copied into RAM, and this client has to
 * fit beside the rest of an Ethernet image's. */
static uint8_t  state;
static uint8_t  sends;               /* messages sent in this state */
static tfw_ms_t deadline;            /* next send or give-up; BOUND: T1 */
static tfw_ms_t req_ms;              /* when the lease began */
static tfw_ms_t lease_ms;            /* how long it runs */
static uint32_t rng;
static uint8_t  xid[4];
static uint8_t  server[4];
static uint8_t  yiaddr[4];
static uint8_t  mask[4];
static uint8_t  router[4];

/* xorshift32: an xid no other client on the segment picks by the same rule
 * and jitter that keeps a fleet's retransmissions apart, not a secret. */
static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void new_xid(void)
{
    uint32_t r = next_rand();
    uint8_t  i;

    for (i = 0u; i < 4u; i++) {
        xid[i] = (uint8_t)(r >> (8u * i));
    }
}

static uint8_t passed(tfw_ms_t now, tfw_ms_t t)
{
    return (uint8_t)((int32_t)(now - t) >= 0);
}

static tfw_ms_t t1(void)
{
    return req_ms + lease_ms / 2u;
}

static tfw_ms_t t2(void)
{
    return req_ms + (lease_ms - lease_ms / 8u);
}

static tfw_ms_t lease_end(void)
{
    return req_ms + lease_ms;
}

/* The magic cookie 99.130.83.99 (RFC 2131 §3), byte i. */
static uint8_t cookie(uint8_t i)
{
    return (uint8_t)((i == 1u) ? 130u : (i == 2u) ? 83u : 99u);
}

static void copy4(uint8_t *dst, const uint8_t *src)
{
    uint8_t i;

    for (i = 0u; i < 4u; i++) {
        dst[i] = src[i];
    }
}

static uint8_t is_zero4(const uint8_t *a)
{
    return (uint8_t)((a[0] | a[1] | a[2] | a[3]) == 0u);
}

static uint8_t equal4(const uint8_t *a, const uint8_t *b)
{
    return (uint8_t)(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]);
}

static void fill(tfw_netcfg_t *cfg)
{
    copy4(cfg->ip, yiaddr);
    copy4(cfg->mask, mask);
    copy4(cfg->gateway, router);
}

static void forget(void)
{
    const uint8_t zero[4] = { 0u, 0u, 0u, 0u };

    copy4(yiaddr, zero);
    copy4(mask, zero);
    copy4(router, zero);
    copy4(server, zero);
    state = S_INIT;
}

/* Gives the socket back and starts the next attempt from INIT. */
static tfw_status_t give_up(tfw_status_t st)
{
    tfw_eth_udp_close();
    forget();
    return st;
}

static void bind_lease(void)
{
    tfw_eth_udp_close();
    state    = S_BOUND;
    deadline = t1();
}

static void schedule(tfw_ms_t now)
{
    tfw_ms_t wait = (tfw_ms_t)RETX_BASE_MS << sends;

    wait += (tfw_ms_t)(next_rand() % (2u * RETX_JITTER + 1u));
    deadline = now + wait - RETX_JITTER;
    sends++;
}

/* The next request while renewing or rebinding: half the time left until
 * `until`, at least 60 s, and never past it (§4.4.5). */
static void schedule_half(tfw_ms_t now, tfw_ms_t until)
{
    tfw_ms_t wait = (until - now) / 2u;

    if (wait < RENEW_MIN_MS) {
        wait = RENEW_MIN_MS;
    }
    deadline = now + wait;
    if ((int32_t)(deadline - until) > 0) {
        deadline = until;
    }
}

static void put(uint8_t b, uint16_t *n)
{
    tfw_eth_udp_put(*n, b);
    (*n)++;
}

static void put_n(const uint8_t *p, uint8_t len, uint16_t *n)
{
    uint8_t i;

    for (i = 0u; i < len; i++) {
        put(p ? p[i] : 0u, n);
    }
}

/* One message to the servers' port, its fields by Table 5: DHCPDISCOVER,
 * DHCPREQUEST, or DHCPDECLINE of the probed address. Before it holds a lease
 * the client asks for a broadcast reply and names the offer it takes; holding
 * one, it gives its address in ciaddr and, renewing, sends to the server that
 * granted it (§4.3.2). */
static uint8_t send_msg(const tfw_netcfg_t *cfg, uint8_t type)
{
    const uint8_t bcast[4] = { 255u, 255u, 255u, 255u };
    uint8_t       held = (uint8_t)(state >= S_BOUND);
    uint16_t      n = 0u;
    uint8_t       i;

    put(1u, &n);                                           /* op: BOOTREQUEST */
    put(1u, &n);                                           /* htype, hlen */
    put(6u, &n);
    put_n(0, 1u, &n);                                      /* hops */
    put_n(xid, 4u, &n);
    put_n(0, 2u, &n);                                      /* secs */
    put((held || type == MSG_DECLINE) ? 0u : 0x80u, &n);   /* flags: BROADCAST */
    put(0u, &n);
    put_n(held ? yiaddr : 0, 4u, &n);                      /* ciaddr */
    put_n(0, 12u, &n);                                     /* yiaddr .. giaddr */
    put_n(cfg->mac, 6u, &n);
    while (n < BOOTP_FIXED) {                              /* chaddr pad, sname, file */
        put(0u, &n);
    }
    for (i = 0u; i < 4u; i++) {
        put(cookie(i), &n);
    }
    put(OPT_TYPE, &n);
    put(1u, &n);
    put(type, &n);
    if (type != MSG_DECLINE) {
        put(OPT_PARAMS, &n);
        put(2u, &n);
        put(OPT_MASK, &n);
        put(OPT_ROUTER, &n);
    }
    if (state == S_REQUESTING || type == MSG_DECLINE) {
        put(OPT_REQ_IP, &n);
        put(4u, &n);
        put_n(yiaddr, 4u, &n);
        put(OPT_SERVER, &n);
        put(4u, &n);
        put_n(server, 4u, &n);
    }
    put(OPT_END, &n);
    while (n < BOOTP_MIN) {
        put(OPT_PAD, &n);
    }
    return tfw_eth_udp_send(n, (state == S_RENEWING) ? server : bcast, PORT_SERVER);
}

/* One byte of the fixed part at offset i: 0 when it rules the reply out. */
static uint8_t fixed(uint16_t i, uint8_t b, const tfw_netcfg_t *cfg, reply_t *r)
{
    if (i == 0u) {
        return (uint8_t)(b == 2u);                         /* BOOTREPLY */
    }
    if (i >= 4u && i < 8u) {
        return (uint8_t)(b == xid[i - 4u]);
    }
    if (i >= 16u && i < 20u) {
        r->yiaddr[i - 16u] = b;
    } else if (i >= 28u && i < 34u) {
        return (uint8_t)(b == cfg->mac[i - 28u]);
    } else if (i >= BOOTP_FIXED) {
        return (uint8_t)(b == cookie((uint8_t)(i - BOOTP_FIXED)));
    }
    return 1u;
}

/* Where an option's value goes, and the bit its length earns, or NULL. */
static uint8_t *slot(reply_t *r, uint8_t code, uint8_t len, uint8_t *bit)
{
    /* An option of the wrong length (RFC 2132 3.3, 3.5, 9.2, 9.7: 4 octets,
     * the router list a multiple of 4) is ignored whole: no slot, so nothing
     * is stored and an earlier good copy keeps its value and its got-bit. */
    *bit = 0u;
    switch (code) {
    case OPT_SERVER:
        if (len != 4u) {
            return 0;
        }
        *bit = GOT_SERVER;
        return r->server;
    case OPT_LEASE:
        if (len != 4u) {
            return 0;
        }
        *bit = GOT_LEASE;
        return r->lease;
    case OPT_MASK:
        if (len != 4u) {
            return 0;
        }
        *bit = GOT_MASK;
        return r->mask;
    case OPT_ROUTER:                                       /* the first router */
        if (len < 4u || (len & 3u) != 0u) {
            return 0;
        }
        *bit = GOT_ROUTER;
        return r->router;
    case OPT_TYPE:
        return &r->type;
    default:
        return 0;
    }
}

/* Reads one datagram whole and hands it back to the controller. 1 when r holds
 * a reply to this client with its options parsed, 0 when there was none or it
 * was not one, and 0xFF when the socket holds less than the datagram's header
 * says, which the controller never does with a datagram it has whole. */
static uint8_t take(const tfw_netcfg_t *cfg, reply_t *r)
{
    uint16_t avail = tfw_eth_udp_avail();
    uint8_t  ok, code = 0u, len = 0u, at = 0u, phase = 0u, bit = 0u;
    uint8_t *dst = 0;
    uint16_t n, i;

    if (avail == 0u) {
        return 0u;
    }
    if (avail < UDP_HEADER) {
        return 0xFFu;
    }
    n = (uint16_t)(((uint16_t)tfw_eth_udp_get(6u) << 8) | tfw_eth_udp_get(7u));
    if (n > (uint16_t)(avail - UDP_HEADER)) {
        return 0xFFu;
    }
    ok = (uint8_t)(tfw_eth_udp_get(4u) == 0u && tfw_eth_udp_get(5u) == PORT_SERVER &&
                   n > BOOTP_FIXED + 4u);
    r->type = 0u;
    r->got  = 0u;
    for (i = 0u; ok && !(r->got & GOT_END) && i < n; i++) {
        uint8_t b = tfw_eth_udp_get((uint16_t)(UDP_HEADER + i));

        if (i < BOOTP_FIXED + 4u) {
            ok = fixed(i, b, cfg, r);
        } else if (phase == 0u) {                          /* option code */
            if (b == OPT_END) {
                r->got |= GOT_END;
            } else if (b == OPT_OVERLOAD) {
                ok = 0u;               /* options in sname/file: not read */
            } else if (b != OPT_PAD) {
                code  = b;
                phase = 1u;
            }
        } else if (phase == 1u) {                          /* option length */
            len   = b;
            at    = 0u;
            dst   = slot(r, code, len, &bit);
            phase = len ? 2u : 0u;
            if (code == OPT_TYPE && len != 1u) {
                ok = 0u;
            }
        } else {                                           /* option value */
            if (dst && at < 4u) {
                dst[at] = b;
            }
            if (++at == len) {
                r->got |= bit;
                phase = 0u;
            }
        }
    }
    tfw_eth_udp_done((uint16_t)(UDP_HEADER + n));
    return (uint8_t)(ok && (r->got & GOT_END) && r->type != 0u);
}

/* Acts on one reply in the current state. TFW_OK once bound again by a renewal,
 * TFW_ERR_NACK on the server's DHCPNAK, TFW_ERR_BUSY otherwise -- including
 * the DHCPACK of a first lease, which sends the probe. */
static tfw_status_t on_reply(const reply_t *r, tfw_ms_t now)
{
    uint32_t secs;

    if (state == S_SELECTING) {
        if (r->type == MSG_OFFER && (r->got & GOT_SERVER) && !is_zero4(r->yiaddr)) {
            copy4(server, r->server);
            copy4(yiaddr, r->yiaddr);
            state    = S_REQUESTING;
            sends    = 0u;
            deadline = now;            /* the DHCPREQUEST goes on this poll */
        }
        return TFW_ERR_BUSY;
    }
    if (state != S_REQUESTING && state != S_RENEWING && state != S_REBINDING) {
        return TFW_ERR_BUSY;           /* nothing asked for */
    }
    if (!(r->got & GOT_SERVER) ||
        (state != S_REBINDING && !equal4(r->server, server))) {
        return TFW_ERR_BUSY;           /* another server's answer */
    }
    if (r->type == MSG_NAK) {
        return give_up(TFW_ERR_NACK);
    }
    if (r->type != MSG_ACK || !(r->got & GOT_LEASE) || is_zero4(r->yiaddr) ||
        (state == S_REQUESTING ? !(r->got & GOT_MASK) : !equal4(r->yiaddr, yiaddr))) {
        return TFW_ERR_BUSY;           /* not a lease the link can use */
    }
    secs = ((uint32_t)r->lease[0] << 24) | ((uint32_t)r->lease[1] << 16) |
           ((uint32_t)r->lease[2] << 8) | r->lease[3];
    if (secs > LEASE_MAX_S) {
        secs = LEASE_MAX_S;
    }
    if (state == S_RENEWING) {
        req_ms = t1();                 /* before the first renewal request */
    } else if (state == S_REBINDING) {
        req_ms = t2();
    }
    lease_ms = (tfw_ms_t)secs * 1000u;
    copy4(server, r->server);
    if (state != S_REQUESTING) {       /* the address the connection is on */
        bind_lease();
        return TFW_OK;
    }
    copy4(yiaddr, r->yiaddr);
    copy4(mask, r->mask);
    if (r->got & GOT_ROUTER) {
        copy4(router, r->router);
    }
    state    = S_PROBING;
    deadline = now + PROBE_MS;
    tfw_eth_udp_put(0u, 0u);
    if (!tfw_eth_udp_send(1u, yiaddr, PORT_DISCARD)) {
        return give_up(TFW_ERR_INTERNAL);
    }
    return TFW_ERR_BUSY;
}

/* What the probe of the offered address found: free and bound, in use and
 * declined, or nothing yet. */
static tfw_status_t probed(tfw_netcfg_t *cfg, tfw_ms_t now)
{
    uint8_t sent = tfw_eth_udp_sent();

    if (sent == TFW_ETH_SENT_NOARP) {
        bind_lease();
        fill(cfg);
        return TFW_OK;
    }
    if (sent == TFW_ETH_SENT_OK) {
        if (!send_msg(cfg, MSG_DECLINE)) {
            return give_up(TFW_ERR_INTERNAL);
        }
        state    = S_DECLINED;
        deadline = now + DECLINE_MS;
        return TFW_ERR_BUSY;
    }
    if (passed(now, deadline)) {
        return give_up(TFW_ERR_INTERNAL);  /* the controller never finished it */
    }
    return TFW_ERR_BUSY;
}

static tfw_status_t start(tfw_netcfg_t *cfg, tfw_ms_t now)
{
    tfw_status_t st;
    uint8_t      i;

    forget();
    fill(cfg);                         /* no address until there is a lease */
    rng = (uint32_t)now | 1u;
    for (i = 0u; i < 6u; i++) {
        rng = (rng << 5) ^ (rng >> 27) ^ cfg->mac[i];
    }
    if (rng == 0u) {
        rng = 1u;
    }
    new_xid();
    st = tfw_eth_udp_open(cfg, PORT_CLIENT);
    if (st != TFW_OK) {
        return st;
    }
    state    = S_SELECTING;
    sends    = 0u;
    deadline = now + WAIT_FIRST_MS + (tfw_ms_t)(next_rand() % (WAIT_SPAN_MS + 1u));
    return TFW_ERR_BUSY;
}

tfw_status_t tfw_dhcp_poll(tfw_netcfg_t *cfg)
{
    tfw_ms_t now = tfw_hal_millis();
    reply_t  r;
    uint8_t  got;

    if (!(cfg->flags & TFW_NETCFG_DHCP)) {
        forget();
        return TFW_OK;
    }
    if (state >= S_BOUND) {
        if (!passed(now, lease_end())) {
            /* The connection went while the lease was being renewed: the
             * link's next connect resets the controller, so ask again as
             * soon as it is up. */
            if (state != S_BOUND) {
                tfw_eth_udp_close();
                state    = S_BOUND;
                deadline = now;
            }
            fill(cfg);
            return TFW_OK;
        }
        (void)give_up(TFW_OK);
    }
    if (state == S_DECLINED) {
        if (!passed(now, deadline)) {
            return TFW_ERR_BUSY;
        }
        state = S_INIT;
    }
    if (state == S_INIT) {
        return start(cfg, now);
    }
    if (state == S_PROBING) {
        return probed(cfg, now);
    }

    got = take(cfg, &r);
    if (got == 0xFFu) {
        return give_up(TFW_ERR_INTERNAL);
    }
    if (got) {
        tfw_status_t st = on_reply(&r, now);

        if (st != TFW_ERR_BUSY || state == S_PROBING) {
            return st;
        }
    }

    if (!passed(now, deadline)) {
        return TFW_ERR_BUSY;
    }
    if (sends == RETX_SENDS) {
        return give_up(TFW_ERR_TIMEOUT);
    }
    if (state == S_REQUESTING && sends == 0u) {
        req_ms = now;
    }
    if (!send_msg(cfg, state == S_SELECTING ? MSG_DISCOVER : MSG_REQUEST)) {
        return give_up(TFW_ERR_INTERNAL);
    }
    schedule(now);
    return TFW_ERR_BUSY;
}

uint8_t tfw_dhcp_due(void)
{
    if (state < S_BOUND) {
        return 0u;
    }
    if (passed(tfw_hal_millis(), deadline)) {
        return 1u;
    }
    return (uint8_t)(state != S_BOUND && tfw_eth_udp_avail() != 0u);
}

tfw_status_t tfw_dhcp_renew(const tfw_netcfg_t *cfg)
{
    tfw_ms_t now = tfw_hal_millis();

    if (state < S_BOUND) {
        return TFW_OK;                 /* a static address: nothing to keep */
    }
    if (state != S_BOUND) {
        reply_t r;
        uint8_t got = take(cfg, &r);

        if (got == 0xFFu) {
            return give_up(TFW_ERR_INTERNAL);
        }
        if (got) {
            tfw_status_t st = on_reply(&r, now);

            if (st != TFW_ERR_BUSY) {
                return st;
            }
        }
    }
    if (!passed(now, deadline)) {
        return TFW_OK;
    }
    if (passed(now, lease_end())) {
        return give_up(TFW_ERR_TIMEOUT);
    }
    if (state == S_BOUND) {
        if (tfw_eth_udp_beside(PORT_CLIENT) != TFW_OK) {
            schedule_half(now, lease_end());   /* the lease still runs: later */
            return TFW_OK;
        }
        new_xid();
        state = S_RENEWING;
    }
    if (state == S_RENEWING && passed(now, t2())) {
        state = S_REBINDING;
    }
    (void)send_msg(cfg, MSG_REQUEST);  /* no room: the next one goes anyway */
    schedule_half(now, (state == S_RENEWING) ? t2() : lease_end());
    return TFW_OK;
}
