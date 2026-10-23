/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The DHCP client (src/net/dhcp.c) fed what anyone on the LAN can send it:
 * random datagrams, its own server's replies truncated at every length and
 * flipped at every bit, option lists that run past the datagram, lose their
 * end option, drown in pads or repeat themselves. Every input is judged twice,
 * by an independent reading of RFC 2131 and the row (judge() below) and by the
 * client on the software W5100 / W5500, and the two must agree: a reply the
 * row refuses changes nothing, and a lease is taken only from a reply that
 * carries one, with exactly the address, mask and gateway that reply names.
 *
 * The generator is a fixed-seed xorshift, so every run is the same run and a
 * failure names its case and iteration. Under ASan and UBSan (make test-asan)
 * the same inputs also prove no read or write leaves its buffer.
 *
 * Judged by the row's rules and the client's own: one reply for its xid and
 * MAC, BOOTREPLY with the magic cookie, an end option, a message type of
 * length 1, no option overload, from port 67; while SELECTING an offer with a
 * server id and an address; while REQUESTING an ack or nak from the chosen
 * server, an ack with a lease time, a mask and an address; while RENEWING an
 * ack for the bound address. A later well-formed instance of an option
 * replaces an earlier one.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/eth.h"
#include "tfw/hal.h"
#include "tfw/board.h"
#include "w5x00_model.h"

#define CHIP       ((uint8_t)W5X_TEST_CHIP)
#define SEED       0x7F4A7C15u
#define RUNS       800u                /* random replies per phase */

#define S_SR       0x03u               /* socket status, socket 1 selected */
#define SR_UDP     0x22u

#define HDR        8u                  /* what the chip puts before a datagram */
#define FIXED      240u                /* BOOTP fields and the magic cookie */
#define MSG_LEN    300u                /* a reply padded to the BOOTP minimum */
#define DGRAM_MAX  1400u

#define LEASE_S    3600u
#define T1_MS      (LEASE_S * 1000u / 2u)

/* Where the client is when the reply arrives. */
#define PH_SELECTING   0u
#define PH_REQUESTING  1u
#define PH_RENEWING    2u

/* What becomes of a reply. */
#define W_REFUSE  0u                   /* dropped, nothing changes */
#define W_TAKE    1u                   /* an offer taken up, or a lease */
#define W_NAK     2u                   /* the lease process starts again */
#define W_DROP    3u                   /* a datagram shorter than it says */

typedef struct {
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    uint8_t server[4];
} lease_t;

static const uint8_t mac[6]     = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u };
static const uint8_t srv_a[4]   = { 192u, 0u, 2u, 1u };
static const uint8_t srv_b[4]   = { 192u, 0u, 2u, 2u };
static const uint8_t offered[4] = { 192u, 0u, 2u, 77u };
static const uint8_t netmask[4] = { 255u, 255u, 255u, 0u };
static const uint8_t gw[4]      = { 192u, 0u, 2u, 254u };
static const uint8_t lease[4]   = { 0u, 0u, (uint8_t)(LEASE_S >> 8), (uint8_t)LEASE_S };
static const uint8_t none[4]    = { 0u, 0u, 0u, 0u };

static tfw_ms_t     now_ms;
static tfw_netcfg_t cfg;
static uint8_t      xid[4];
static uint32_t     rng;
static uint8_t      d[HDR + DGRAM_MAX];       /* the datagram as the chip hands it up */
static char         where[96];

tfw_ms_t tfw_hal_millis(void) { return now_ms; }

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t below(uint32_t n) { return next() % n; }
static uint8_t  any8(void)        { return (uint8_t)(next() >> 24); }

static void at(const char *what, uint32_t i)
{
    (void)snprintf(where, sizeof where, "seed 0x%08X, %s, iteration %lu",
                   SEED, what, (unsigned long)i);
}

static void block(void)
{
    static const uint8_t own_ip[4] = { 198u, 51u, 100u, 10u };

    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.mac, mac, 6u);
    memcpy(cfg.ip, own_ip, 4u);
    memcpy(cfg.mask, netmask, 4u);
    memcpy(cfg.gateway, own_ip, 4u);
    cfg.peer[0] = 192u;
    cfg.peer[1] = 0u;
    cfg.peer[2] = 2u;
    cfg.peer[3] = 20u;
    cfg.port  = 7020u;
    cfg.flags = TFW_NETCFG_DHCP;
}

/* ------------------------------------------------------------ the replies */

static uint16_t opt(uint16_t n, uint8_t code, uint8_t len, const uint8_t *v)
{
    uint8_t *m = d + HDR;
    uint8_t  i;

    m[n++] = code;
    m[n++] = len;
    for (i = 0u; i < len; i++) {
        m[n++] = v ? v[i] : any8();
    }
    return n;
}

/* The UDP header the chip prepends: the sender, its port and the length. */
static uint16_t header(uint16_t n)
{
    memcpy(d, srv_a, 4u);
    d[4] = 0u;
    d[5] = 67u;
    d[6] = (uint8_t)(n >> 8);
    d[7] = (uint8_t)n;
    return (uint16_t)(HDR + n);
}

static void fixed_part(const uint8_t *yi)
{
    uint8_t *m = d + HDR;

    memset(d, 0, sizeof d);
    m[0] = 2u;                                              /* BOOTREPLY */
    m[1] = 1u;
    m[2] = 6u;
    memcpy(m + 4, xid, 4u);
    memcpy(m + 16, yi, 4u);
    memcpy(m + 20, srv_a, 4u);
    memcpy(m + 28, mac, 6u);
    m[236] = 99u;
    m[237] = 130u;
    m[238] = 83u;
    m[239] = 99u;
}

static uint16_t every_option(uint16_t n, uint8_t type)
{
    n = opt(n, 53u, 1u, &type);
    n = opt(n, 54u, 4u, srv_a);
    n = opt(n, 51u, 4u, lease);
    n = opt(n, 1u, 4u, netmask);
    return opt(n, 3u, 4u, gw);
}

/* The server's own reply, as it would send it: the datagram's length with its
 * header. */
static uint16_t valid(uint8_t type)
{
    uint16_t n;

    fixed_part(offered);
    n = every_option(FIXED, type);
    d[HDR + n] = 255u;
    return header(MSG_LEN);
}

static uint8_t phase_type(uint8_t phase)
{
    return (uint8_t)((phase == PH_SELECTING) ? 2u : 5u);
}

static uint16_t random_option(uint16_t n, uint8_t phase)
{
    static const uint8_t known[5] = { 1u, 3u, 51u, 53u, 54u };
    static const uint8_t right[5] = { 4u, 4u, 4u, 1u, 4u };
    uint8_t  *m = d + HDR;
    uint8_t   v[4], k, len, code;

    if (n + 2u + 255u > DGRAM_MAX) {
        return n;
    }
    switch (below(12u)) {
    case 0u:                                                /* a run of pads */
        for (k = (uint8_t)(1u + below(40u)); k; k--) {
            m[n++] = 0u;
        }
        return n;
    case 1u:
        v[0] = (uint8_t)(below(2u) ? phase_type(phase) : below(9u));
        return opt(n, 53u, 1u, v);
    case 2u:
        return opt(n, 54u, 4u, below(4u) ? srv_a : below(2u) ? srv_b : 0);
    case 3u:
        return opt(n, 51u, 4u, 0);
    case 4u:
        return opt(n, 1u, 4u, below(2u) ? netmask : 0);
    case 5u:
        return opt(n, 3u, (uint8_t)(4u * (1u + below(3u))), 0);
    case 6u:                                                /* a known code, misshapen */
        k = (uint8_t)below(5u);
        do {
            len = (uint8_t)below(9u);
        } while (len == right[k] || (known[k] == 3u && len != 0u && (len & 3u) == 0u));
        return opt(n, known[k], len, 0);
    case 7u:                                                /* a code the client ignores */
        do {
            code = any8();
        } while (code == 0u || code == 255u || code == 52u || code == 1u || code == 3u ||
                 code == 51u || code == 53u || code == 54u);
        return opt(n, code, (uint8_t)below(41u), 0);
    case 8u:
        if (below(2u)) {
            v[0] = (uint8_t)(1u + below(3u));
            return opt(n, 52u, 1u, v);                      /* option overload */
        }
        return n;
    case 9u:                                                /* a length past everything */
        m[n++] = known[below(5u)];
        m[n++] = 255u;
        return n;
    case 10u:                                               /* an end, then noise */
        m[n++] = 255u;
        for (k = (uint8_t)below(20u); k; k--) {
            m[n++] = any8();
        }
        return n;
    default:
        return every_option(n, phase_type(phase));
    }
}

/* A reply drawn at random. Mostly a well-formed BOOTREPLY for this client
 * with a random option list, sometimes one damaged field, sometimes noise
 * from end to end; the datagram's length is the one it claims, cut short or
 * not. */
static uint16_t random_reply(uint8_t phase)
{
    uint8_t *m = d + HDR;
    uint8_t  yi[4], k;
    uint16_t n, i;

    if (below(8u) == 0u) {
        memset(d, 0, sizeof d);
        n = (uint16_t)below(DGRAM_MAX + 1u);
        for (i = 0u; i < n; i++) {
            m[i] = any8();
        }
        return header(n);
    }
    switch (below(16u)) {
    case 0u:
        memcpy(yi, none, 4u);
        break;
    case 1u:
    case 2u:
    case 3u:
        for (k = 0u; k < 4u; k++) {
            yi[k] = any8();
        }
        yi[0] = (uint8_t)(yi[0] | 1u);       /* the model answers ARP for x.0.0.0 */
        break;
    default:
        memcpy(yi, offered, 4u);
        break;
    }
    fixed_part(yi);
    if (below(4u) == 0u) {                                  /* one field damaged */
        static const uint8_t field[11] = { 0u, 4u, 5u, 6u, 7u, 28u, 31u, 33u, 236u, 238u, 239u };

        m[field[below(11u)]] ^= (uint8_t)(1u + below(255u));
    }
    n = FIXED;
    if (below(2u)) {
        n = every_option(n, phase_type(phase));
    }
    for (k = (uint8_t)below(10u); k; k--) {
        n = random_option(n, phase);
    }
    if (below(4u) && n < DGRAM_MAX) {
        m[n++] = 255u;
    }
    if (below(2u) && n < MSG_LEN) {
        n = MSG_LEN;
    }
    if (below(8u) == 0u) {
        n = (uint16_t)below(n + 1u);
    }
    return header(n);
}

/* --------------------------------------------------------------- the judge */

/* RFC 2131 and the row, read independently of the client. An option of a
 * known code with the wrong length (RFC 2132) is ignored whole: an earlier
 * well-formed one of the same code stands. */
static uint8_t judge(uint8_t phase, uint16_t delivered, lease_t *w)
{
    const uint8_t *m = d + HDR;
    uint8_t  type = 0u, end = 0u, got_srv = 0u, got_lease = 0u, got_mask = 0u, got_gw = 0u;
    uint8_t  srv[4] = { 0u, 0u, 0u, 0u };
    uint16_t n, i;

    memset(w, 0, sizeof *w);
    if (delivered < HDR) {
        return W_DROP;
    }
    n = (uint16_t)((uint16_t)(d[6] << 8) | d[7]);
    if (n > delivered - HDR) {
        return W_DROP;
    }
    if (d[4] != 0u || d[5] != 67u || n <= FIXED) {
        return W_REFUSE;
    }
    if (m[0] != 2u || memcmp(m + 4, xid, 4u) != 0 || memcmp(m + 28, mac, 6u) != 0 ||
        m[236] != 99u || m[237] != 130u || m[238] != 83u || m[239] != 99u) {
        return W_REFUSE;
    }
    i = FIXED;
    while (i < n) {
        const uint8_t *v;
        uint8_t        code = m[i], len;

        if (code == 0u) {
            i++;
            continue;
        }
        if (code == 255u) {
            end = 1u;
            break;
        }
        if (code == 52u) {
            return W_REFUSE;
        }
        if (i + 1u >= n) {
            break;
        }
        len = m[i + 1u];
        if (code == 53u && len != 1u) {
            return W_REFUSE;
        }
        if (i + 2u + len > n) {
            break;                                          /* runs past the datagram */
        }
        v = m + i + 2u;
        switch (code) {
        case 53u:
            type = v[0];
            break;
        case 54u:                           /* a wrong length is ignored whole */
            if (len == 4u) {
                memcpy(srv, v, 4u);
                got_srv = 1u;
            }
            break;
        case 51u:
            got_lease = (uint8_t)(got_lease | (len == 4u));
            break;
        case 1u:
            if (len == 4u) {
                memcpy(w->mask, v, 4u);
                got_mask = 1u;
            }
            break;
        case 3u:
            if (len >= 4u && (len & 3u) == 0u) {
                memcpy(w->gw, v, 4u);                       /* the first router */
                got_gw = 1u;
            }
            break;
        default:
            break;
        }
        i = (uint16_t)(i + 2u + len);
    }
    if (!end || type == 0u) {
        return W_REFUSE;
    }
    memcpy(w->ip, m + 16, 4u);
    memcpy(w->server, srv, 4u);
    if (!got_gw) {
        memset(w->gw, 0, 4u);
    }
    if (phase == PH_SELECTING) {
        return (uint8_t)((type == 2u && got_srv && memcmp(w->ip, none, 4u) != 0) ? W_TAKE
                                                                               : W_REFUSE);
    }
    if (!got_srv || memcmp(srv, srv_a, 4u) != 0) {
        return W_REFUSE;                                    /* another server's */
    }
    if (type == 6u) {
        return W_NAK;
    }
    if (type != 5u || !got_lease || memcmp(w->ip, none, 4u) == 0) {
        return W_REFUSE;
    }
    if (phase == PH_REQUESTING) {
        return got_mask ? W_TAKE : W_REFUSE;
    }
    return (memcmp(w->ip, offered, 4u) == 0) ? W_TAKE : W_REFUSE;
}

/* -------------------------------------------------------------- the client */

static uint16_t heard(uint8_t *buf, uint16_t cap)
{
    return w5x_model_peer_recv(buf, cap);
}

/* The client from nothing to the phase: a static block resets it, the first
 * DHCPDISCOVER goes once its 1-10 s wait has run, an offer of `offered` from
 * srv_a moves it to REQUESTING, and the ack, the probe and T1 to RENEWING. */
static void reach(uint8_t phase)
{
    tfw_netcfg_t plain;
    uint8_t      buf[2u * MSG_LEN];

    w5x_model_reset(CHIP);
    block();
    plain       = cfg;
    plain.flags = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&plain));
    w5x_model_socket(1u);
    w5x_model_arp_silent(offered);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    now_ms += 10001u;
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT16(MSG_LEN, heard(buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(1u, buf[242]);                  /* DHCPDISCOVER */
    memcpy(xid, buf + 4, 4u);
    if (phase == PH_SELECTING) {
        return;
    }
    w5x_model_peer_send(d, valid(2u));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT16(MSG_LEN, heard(buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(3u, buf[242]);                  /* DHCPREQUEST */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(none, cfg.ip, 4u);
    if (phase == PH_REQUESTING) {
        return;
    }
    w5x_model_peer_send(d, valid(5u));
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, tfw_dhcp_poll(&cfg));   /* the probe */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    now_ms += T1_MS;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_EQUAL_UINT16(MSG_LEN, heard(buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(3u, buf[242]);                  /* the renewal */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, buf + 12, 4u);   /* ciaddr */
    memcpy(xid, buf + 4, 4u);
    TEST_ASSERT_EQUAL_UINT8(SR_UDP, w5x_model_sock(S_SR));
}

/* Hands the datagram in d up to the client and reads what it did with it. */
static uint8_t feed(uint8_t phase, uint16_t delivered, lease_t *seen)
{
    uint8_t      buf[2u * MSG_LEN];
    uint8_t      sends;
    tfw_status_t st;

    memset(seen, 0, sizeof *seen);
    if (delivered >= HDR + 20u && d[HDR + 16u] != 0u) {
        w5x_model_arp_silent(d + HDR + 16u);                /* the address is free */
    }
    sends = w5x_model_sends();
    w5x_model_peer_send(d, delivered);
    if (phase == PH_RENEWING) {
        st = tfw_dhcp_renew(&cfg);
        if (st == TFW_ERR_NACK) {
            return W_NAK;
        }
        if (st == TFW_ERR_INTERNAL) {
            return W_DROP;
        }
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, where);
        if (w5x_model_sock(S_SR) == SR_UDP) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(sends, w5x_model_sends(), where);
            return W_REFUSE;                                /* still renewing */
        }
        memcpy(seen->ip, offered, 4u);                      /* renewed: bound, socket closed */
        return W_TAKE;
    }
    st = tfw_dhcp_poll(&cfg);
    if (st == TFW_ERR_NACK) {
        return W_NAK;
    }
    if (st == TFW_ERR_INTERNAL) {
        return W_DROP;
    }
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BUSY, st, where);
    /* Nothing is configured before a lease, whatever arrived. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(none, cfg.ip, 4u, where);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(none, cfg.mask, 4u, where);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(none, cfg.gateway, 4u, where);
    if (phase == PH_SELECTING) {
        if (w5x_model_sends() == sends) {
            return W_REFUSE;
        }
        /* An offer taken up is requested on the same poll, naming it. */
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(MSG_LEN, heard(buf, sizeof buf), where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(3u, buf[242], where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(50u, buf[247], where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(54u, buf[253], where);
        memcpy(seen->ip, buf + 249, 4u);
        memcpy(seen->server, buf + 255, 4u);
        return W_TAKE;
    }
    st = tfw_dhcp_poll(&cfg);
    if (st == TFW_ERR_BUSY) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(sends, w5x_model_sends(), where);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(none, cfg.ip, 4u, where);
        return W_REFUSE;
    }
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, st, where);
    memcpy(seen->ip, cfg.ip, 4u);
    memcpy(seen->mask, cfg.mask, 4u);
    memcpy(seen->gw, cfg.gateway, 4u);
    return W_TAKE;
}

/* Judges the datagram in d, feeds it, and holds the client to the verdict.
 * A client that moved on is brought back to the phase for the next one. */
static void check(uint8_t phase, uint16_t delivered)
{
    lease_t w, seen;
    uint8_t want = judge(phase, delivered, &w), got;

    got = feed(phase, delivered, &seen);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(want, got, where);
    if (got == W_TAKE) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(w.ip, seen.ip, 4u, where);
        if (phase == PH_SELECTING) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(w.server, seen.server, 4u, where);
        } else if (phase == PH_REQUESTING) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(w.mask, seen.mask, 4u, where);
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(w.gw, seen.gw, 4u, where);
        }
    }
    if (got != W_REFUSE) {
        reach(phase);
    }
}

void setUp(void)
{
    rng     = SEED;
    now_ms  = 100000u;
    reach(PH_SELECTING);
}

void tearDown(void) {}

/* ----------------------------------------------------------------- tests */

static void truncations(uint8_t phase)
{
    uint16_t len, k;

    reach(phase);
    for (k = 1u; k < HDR; k++) {                            /* not even a header */
        at("header cut", k);
        (void)valid(phase_type(phase));
        check(phase, k);
    }
    for (len = 0u; len <= MSG_LEN; len++) {
        at("cut and said so", len);
        (void)valid(phase_type(phase));
        check(phase, header(len));
        at("cut, the header claiming all", len);
        (void)valid(phase_type(phase));
        check(phase, (uint16_t)(HDR + len));
    }
}

static void test_a_reply_cut_at_every_length_is_refused_until_it_ends(void)
{
    truncations(PH_SELECTING);
    truncations(PH_REQUESTING);
    truncations(PH_RENEWING);
}

static void flips(uint8_t phase)
{
    uint16_t bit, n;

    reach(phase);
    for (bit = 0u; bit < (uint16_t)((HDR + MSG_LEN) * 8u); bit++) {
        at("one bit flipped", bit);
        n = valid(phase_type(phase));
        d[bit >> 3] ^= (uint8_t)(1u << (bit & 7u));
        /* The chip writes the length; it can overstate (a fault) but never
         * leave part of a datagram behind for the next. */
        if (((uint16_t)(d[6] << 8) | d[7]) < MSG_LEN) {
            continue;
        }
        check(phase, n);
    }
}

static void test_a_reply_with_any_one_bit_flipped_is_judged_by_what_it_now_says(void)
{
    flips(PH_SELECTING);
    flips(PH_REQUESTING);
    flips(PH_RENEWING);
}

static void randoms(uint8_t phase, uint32_t runs)
{
    uint32_t i;

    reach(phase);
    for (i = 0u; i < runs; i++) {
        at(phase == PH_SELECTING ? "random, selecting" :
           phase == PH_REQUESTING ? "random, requesting" : "random, renewing", i);
        check(phase, random_reply(phase));
    }
}

static void test_random_replies_are_refused_unless_the_row_takes_them(void)
{
    randoms(PH_SELECTING, RUNS);
    randoms(PH_REQUESTING, RUNS);
    randoms(PH_RENEWING, RUNS / 2u);
}

/* Pads before the options, between them and after the end, up to a datagram
 * of DGRAM_MAX; and pads with no end at all. */
static void test_a_flood_of_pads_neither_hides_nor_forges_a_reply(void)
{
    uint8_t  phase;
    uint16_t pads, n, k;

    for (phase = PH_SELECTING; phase <= PH_RENEWING; phase++) {
        reach(phase);
        for (pads = 0u; 2u * pads + MSG_LEN <= DGRAM_MAX; pads = (uint16_t)(pads + 37u)) {
            at("pads first", pads);
            fixed_part(offered);
            n = (uint16_t)(FIXED + pads);
            n = every_option(n, phase_type(phase));
            d[HDR + n++] = 255u;
            check(phase, header((uint16_t)(n + pads)));
            at("pads and no end", pads);
            fixed_part(offered);
            n = every_option(FIXED, phase_type(phase));
            for (k = 0u; k < pads && n < DGRAM_MAX; k++) {
                d[HDR + n++] = 0u;
            }
            check(phase, header(n));
        }
    }
}

/* Every option the client reads, its length running past the datagram by
 * every amount, with and without an end byte after where it would stop. */
static void test_an_option_running_past_the_datagram_is_refused(void)
{
    static const uint8_t codes[6] = { 1u, 3u, 51u, 53u, 54u, 12u };
    uint8_t  phase, c;
    uint16_t over, n, len;

    for (phase = PH_SELECTING; phase <= PH_REQUESTING; phase++) {
        reach(phase);
        for (c = 0u; c < 6u; c++) {
            for (over = 1u; over <= 255u; over = (uint16_t)(over + ((over < 32u) ? 1u : 7u))) {
                at("option past the end", (uint32_t)(c * 256u + over));
                fixed_part(offered);
                n = every_option(FIXED, phase_type(phase));
                len = (uint16_t)(255u - (over - 1u) % 255u);
                d[HDR + n++] = codes[c];
                d[HDR + n++] = (uint8_t)len;
                n = (uint16_t)(n + len - over % (len + 1u));
                d[HDR + n - 1u] = 255u;                     /* an end inside the value */
                check(phase, header(n));
            }
        }
    }
}

/* Repeats: the client keeps the last well-formed instance, so a second
 * server id, type, mask or router decides; a reply from another server, or
 * one that turns into a nak or an offer, is that. */
static void test_repeated_and_conflicting_options_are_judged_by_the_last(void)
{
    static const uint8_t types[4] = { 2u, 5u, 6u, 3u };
    const uint8_t *servers[3] = { srv_a, srv_b, 0 };
    uint8_t  phase, a, b, t, u;
    uint16_t n;
    uint32_t i = 0u;

    for (phase = PH_SELECTING; phase <= PH_RENEWING; phase++) {
        reach(phase);
        for (a = 0u; a < 3u; a++) {
            for (b = 0u; b < 3u; b++) {
                for (t = 0u; t < 4u; t++) {
                    for (u = 0u; u < 4u; u++) {
                        uint8_t mask2[4] = { 255u, 255u, 0u, 0u };

                        at("repeated options", i++);
                        fixed_part(offered);
                        n = opt(FIXED, 53u, 1u, &types[t]);
                        n = opt(n, 54u, 4u, servers[a]);
                        n = opt(n, 1u, 4u, netmask);
                        n = opt(n, 51u, 4u, lease);
                        n = opt(n, 3u, 8u, 0);
                        n = opt(n, 54u, 4u, servers[b]);
                        n = opt(n, 53u, 1u, &types[u]);
                        n = opt(n, 1u, 4u, mask2);
                        n = opt(n, 3u, 4u, gw);
                        d[HDR + n++] = 255u;
                        check(phase, header(n));
                    }
                }
            }
        }
    }
}

/* Each known option at every wrong length up to 11, after a well-formed one
 * of the same code: ignored whole, so the reply is taken with the good value
 * (RFC 2132 3.3, 3.5, 9.2, 9.7). */
static void test_a_misshapen_option_after_a_good_one_keeps_the_good_value(void)
{
    static const uint8_t codes[4] = { 54u, 51u, 1u, 3u };
    static const uint8_t junk[11] = { 0xDEu, 0xADu, 0x02u, 0x01u, 0xBEu, 0xEFu,
                                      0x0Bu, 0xADu, 0xF0u, 0x0Du, 0x55u };
    uint8_t  phase, c, len;
    uint16_t n;
    uint32_t i = 0u;

    for (phase = PH_SELECTING; phase <= PH_RENEWING; phase++) {
        reach(phase);
        for (c = 0u; c < 4u; c++) {
            for (len = 0u; len < 12u; len++) {
                lease_t w;

                if (len == 4u || (codes[c] == 3u && len != 0u && (len & 3u) == 0u)) {
                    continue;
                }
                at("misshapen after good", i++);
                fixed_part(offered);
                n = every_option(FIXED, phase_type(phase));
                n = opt(n, codes[c], len, junk);
                d[HDR + n++] = 255u;
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(W_TAKE, judge(phase, header(n), &w), where);
                check(phase, header(n));
            }
        }
    }
}

/* The reduced case (seed 0x7F4A7C15, random replies in SELECTING, iteration
 * 671): a valid offer, then a server id of length 2. The REQUEST must name
 * 192.0.2.1, not 222.173.2.1. */
static void test_a_two_byte_server_id_after_a_good_one_is_ignored(void)
{
    static const uint8_t dead[2] = { 0xDEu, 0xADu };
    lease_t  seen;
    uint16_t n;

    at("server id of length 2", 0u);
    fixed_part(offered);
    n = every_option(FIXED, 2u);
    n = opt(n, 54u, 2u, dead);
    d[HDR + n++] = 255u;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(W_TAKE, feed(PH_SELECTING, header(n), &seen), where);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(srv_a, seen.server, 4u, where);
}

/* The fields the row names, each wrong on its own, in every phase: none of
 * them is a reply to this client, and none moves it. */
static void test_a_reply_for_another_client_or_of_the_wrong_kind_moves_nothing(void)
{
    static const uint16_t fields[9] = { 0u, 4u, 7u, 28u, 33u, 236u, 239u, 1u, 2u };
    uint8_t  phase, f, x;
    uint16_t n;

    for (phase = PH_SELECTING; phase <= PH_RENEWING; phase++) {
        reach(phase);
        for (f = 0u; f < 9u; f++) {
            for (x = 0u; x < 16u; x++) {                    /* each bit, then any value */
                at("one field wrong", (uint32_t)(f * 16u + x));
                n = valid(phase_type(phase));
                d[HDR + fields[f]] ^= (uint8_t)((x < 8u) ? (1u << x) : (1u + below(255u)));
                check(phase, n);
            }
        }
        at("no server id", 0u);
        fixed_part(offered);
        n = opt(FIXED, 53u, 1u, (const uint8_t[]){ phase_type(phase) });
        n = opt(n, 51u, 4u, lease);
        n = opt(n, 1u, 4u, netmask);
        d[HDR + n++] = 255u;
        TEST_ASSERT_EQUAL_UINT8(W_REFUSE, judge(phase, header(MSG_LEN), &(lease_t){ 0 }));
        check(phase, header(MSG_LEN));
        at("from port 68", 0u);
        n = valid(phase_type(phase));
        d[5] = 68u;
        check(phase, n);
    }
}

/* The bound lease survives every refused reply while it is being renewed:
 * after the flood a good ack still renews it, and the link is handed the
 * same address, mask and gateway it had. */
static void test_the_bound_lease_survives_hostile_replies_while_renewing(void)
{
    uint32_t i;
    uint16_t n;

    reach(PH_RENEWING);
    for (i = 0u; i < RUNS / 2u; i++) {
        lease_t w;

        at("hostile while renewing", i);
        n = random_reply(PH_RENEWING);
        if (judge(PH_RENEWING, n, &w) != W_REFUSE) {
            continue;
        }
        check(PH_RENEWING, n);
    }
    w5x_model_peer_send(d, valid(5u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_renew(&cfg));
    TEST_ASSERT_FALSE(w5x_model_sock(S_SR) == SR_UDP);      /* renewed and bound */
    memset(cfg.ip, 0, 4u);
    memset(cfg.mask, 0, 4u);
    memset(cfg.gateway, 0, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dhcp_poll(&cfg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offered, cfg.ip, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(netmask, cfg.mask, 4u);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(gw, cfg.gateway, 4u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_reply_cut_at_every_length_is_refused_until_it_ends);
    RUN_TEST(test_a_reply_with_any_one_bit_flipped_is_judged_by_what_it_now_says);
    RUN_TEST(test_random_replies_are_refused_unless_the_row_takes_them);
    RUN_TEST(test_a_flood_of_pads_neither_hides_nor_forges_a_reply);
    RUN_TEST(test_an_option_running_past_the_datagram_is_refused);
    RUN_TEST(test_repeated_and_conflicting_options_are_judged_by_the_last);
    RUN_TEST(test_a_misshapen_option_after_a_good_one_keeps_the_good_value);
    RUN_TEST(test_a_two_byte_server_id_after_a_good_one_is_ignored);
    RUN_TEST(test_a_reply_for_another_client_or_of_the_wrong_kind_moves_nothing);
    RUN_TEST(test_the_bound_lease_survives_hostile_replies_while_renewing);
    return UNITY_END();
}
