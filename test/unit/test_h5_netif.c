/*
 * The STM32H5 netif (hal_eth_netif.c) on the vendored lwIP, built for the
 * host: the PHY, MAC and wire it drives are fakes here, lwIP's netif, DHCP and
 * timers are the real ones. A link flap must not restart DHCP: after the lease
 * runs out with no reply, the address is cleared (#584, Codex round 3). An
 * offered address is ARP-probed before it is bound, and declined when another
 * host answers (#584, Codex round 3 on 459ee3f23).
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#ifndef SSIZE_MAX                       /* lwIP's arch.h then keeps unistd.h's ssize_t */
#define SSIZE_MAX LONG_MAX
#endif
#include <sys/wait.h>
#include <unistd.h>

#include "unity.h"
#include "crc16.h"
#include "tfw/eth.h"
#include "tfw/hal.h"
#include "hal_eth_mac.h"
#include "hal_eth_mdio.h"
#include "hal_eth_wire.h"
#include "hal_rng.h"
#include "../../src/caps/sched.h"
#include "../../src/crypto/deviceid_p256.h"
#include "soc_regs.h"

#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"

void tfw_hal_eth_poll(void);

static tfw_ms_t now_ms;
static uint8_t  link_up_flag;
/* PSCSR: speed field bits 4:2, 0x6 = 100 Mbit/s full duplex; or a failed read. */
static uint16_t pscsr_val = 0x0018u;
static uint8_t  pscsr_fail;
static uint8_t  phy_absent;     /* every MDIO address reads 0xFFFF: no PHY */
static uint8_t  mac_reset_fail; /* the MAC never leaves its software reset */
static uint32_t mac_cr;
static uint8_t  mac_running;
static uint8_t  mac_seen[6];            /* the address the MAC was started with */
static unsigned tx_frames;
static uint8_t tx_last[TFW_ETH_BUF_SIZE];   /* the last frame the MAC took */
static size_t tx_last_len;
static unsigned tx_busy;                    /* ring-full answers left; UINT_MAX: forever */

tfw_ms_t tfw_hal_millis(void) { return now_ms; }

/* --- the PHY on MDIO: present at address 0, link as link_up_flag says ----- */
void tfw_h5_eth_mdio_init(void) { }
static unsigned bmsr_latched_low;       /* BMSR reads left that give the latched-low 0 */
int tfw_h5_eth_mdio_read(uint8_t addr, uint8_t reg, uint16_t *val)
{
    if (addr != 0u || phy_absent) {
        *val = 0xFFFFu;
        return 0;
    }
    if (reg == TFW_PHY_REG_BMSR && bmsr_latched_low > 0u) {
        bmsr_latched_low--;             /* the read that clears the latch */
        *val = 0u;
        return 0;
    }
    switch (reg) {
    case TFW_PHY_REG_PHYID1: *val = 0x0007u; break;
    case TFW_PHY_REG_PHYID2: *val = 0xC0F1u; break;
    case TFW_PHY_REG_BMSR:
        *val = link_up_flag ? (uint16_t)(TFW_PHY_BMSR_LINK | TFW_PHY_BMSR_ANDONE) : 0u;
        break;
    case TFW_PHY_REG_PSCSR:
        if (pscsr_fail) {
            return -1;
        }
        *val = pscsr_val;
        break;
    default: *val = 0u; break;
    }
    return 0;
}
int tfw_h5_eth_mdio_write(uint8_t addr, uint8_t reg, uint16_t val)
{
    (void)addr; (void)reg; (void)val;
    return 0;
}

/* --- the MAC: frames sent are counted and the DHCP and ARP ones decoded; a
 * test hands one frame at a time up (the DHCP server, a conflicting host) -- */
/* The offered address, 192.0.2.10: RFC 5737 documentation range. */
static const uint8_t offered_ip[4] = { 192, 0, 2, 10 };
static unsigned dhcp_sent;              /* bit n: a DHCP message of type n went out */
static uint8_t  dhcp_xid[4];            /* the xid of the last one */
static unsigned arp_probes;             /* ARP probes for offered_ip */
static uint8_t  rx_frame[400];
static size_t   rx_len;

int tfw_eth_mac_reset(tfw_eth_mac_t *m, uint32_t spin) { (void)m; (void)spin; return mac_reset_fail ? -1 : 0; }
void tfw_eth_mac_init(tfw_eth_mac_t *m, const uint8_t mac[6], uint32_t sp) { (void)m; memcpy(mac_seen, mac, 6); mac_cr = sp; }
void tfw_eth_mac_start(tfw_eth_mac_t *m) { (void)m; mac_running = 1u; }
void tfw_eth_mac_stop(tfw_eth_mac_t *m) { (void)m; mac_running = 0u; }
/* #584 Codex P1: a TCP peer on the wire that answers every keepalive probe
 * the device sends, the answer queued in the rx path as the DMA ring would. */
static const uint8_t ka_peer_mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x30u };
static const uint8_t ka_peer_ip[4]  = { 192u, 0u, 2u, 30u };
static const uint8_t ka_dev_ip[4]   = { 192u, 0u, 2u, 20u };
#define KA_PEER_PORT 40000u
#define KA_DEV_PORT  7000u
static uint32_t        ka_dev_nxt, ka_peer_seq;
static unsigned        ka_probes, ka_answer, ka_errs;
static uint16_t        ka_buffered;
static struct tcp_pcb *ka_pcb;

static void ka_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void ka_put32(uint8_t *p, uint32_t v) { ka_put16(p, v >> 16); ka_put16(p + 2, v); }
static uint32_t ka_get16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t ka_get32(const uint8_t *p) { return (ka_get16(p) << 16) | ka_get16(p + 2); }

static uint32_t ka_sum(const uint8_t *p, size_t n, uint32_t s)
{
    for (size_t i = 0u; i + 1u < n; i += 2u) {
        s += ka_get16(p + i);
    }
    if ((n & 1u) != 0u) {
        s += (uint32_t)p[n - 1u] << 8;
    }
    return s;
}

static uint32_t ka_fold(uint32_t s)
{
    while ((s >> 16) != 0u) {
        s = (s & 0xFFFFu) + (s >> 16);
    }
    return ~s & 0xFFFFu;
}

/* A peer segment to the device: seq ka_peer_seq, ack ka_dev_nxt. */
static void ka_inject_tcp(uint8_t flags, const uint8_t *data, uint16_t dlen)
{
    uint8_t *f = rx_frame, *ip = f + 14, *t = ip + 20;
    uint8_t  ph[12];
    uint32_t tl = 40u + dlen;

    memset(rx_frame, 0, sizeof rx_frame);
    memcpy(f, netif_default->hwaddr, 6);
    memcpy(f + 6, ka_peer_mac, 6);
    f[12] = 0x08u;
    ip[0] = 0x45u; ka_put16(ip + 2, tl); ip[8] = 64u; ip[9] = 6u;
    memcpy(ip + 12, ka_peer_ip, 4);
    memcpy(ip + 16, ka_dev_ip, 4);
    ka_put16(ip + 10, ka_fold(ka_sum(ip, 20u, 0u)));
    ka_put16(t, KA_PEER_PORT);
    ka_put16(t + 2, KA_DEV_PORT);
    ka_put32(t + 4, ka_peer_seq);
    ka_put32(t + 8, (flags & 0x10u) != 0u ? ka_dev_nxt : 0u);
    t[12] = 0x50u; t[13] = flags; ka_put16(t + 14, 4096u);
    if (dlen > 0u) {
        memcpy(t + 20, data, dlen);
    }
    memcpy(ph, ka_peer_ip, 4);
    memcpy(ph + 4, ka_dev_ip, 4);
    ph[8] = 0u; ph[9] = 6u; ka_put16(ph + 10, 20u + dlen);
    ka_put16(t + 16, ka_fold(ka_sum(t, 20u + dlen, ka_sum(ph, 12u, 0u))));
    rx_len = (14u + tl < 60u) ? 60u : 14u + tl;
}

static void ka_inject_arp_request(void)
{
    uint8_t *f = rx_frame, *a = f + 14;

    memset(rx_frame, 0, sizeof rx_frame);
    memset(f, 0xFF, 6);
    memcpy(f + 6, ka_peer_mac, 6);
    f[12] = 0x08u; f[13] = 0x06u;
    a[1] = 1u; a[2] = 0x08u; a[4] = 6u; a[5] = 4u; a[7] = 1u;
    memcpy(a + 8, ka_peer_mac, 6);
    memcpy(a + 14, ka_peer_ip, 4);
    memcpy(a + 24, ka_dev_ip, 4);
    rx_len = 60u;
}

/* Sees the device's segments to the peer: the SYN-ACK sets the device's next
 * sequence number, and a keepalive probe (no data, one below it) is answered. */
static void ka_tx(const uint8_t *f, size_t len)
{
    const uint8_t *t = f + 34;
    uint32_t seq, dlen;

    if (len < 54u || f[12] != 0x08u || f[13] != 0x00u || f[23] != 6u ||
        ka_get16(t + 2) != KA_PEER_PORT) {
        return;
    }
    seq  = ka_get32(t + 4);
    dlen = ka_get16(f + 16) - 20u - (uint32_t)(t[12] >> 4) * 4u;
    if ((t[13] & 0x02u) != 0u) {
        ka_dev_nxt = seq + 1u;
        return;
    }
    if (dlen == 0u && seq + 1u == ka_dev_nxt) {
        ka_probes++;
        if (ka_answer) {
            ka_inject_tcp(0x10u, NULL, 0u);
        }
    }
}

int tfw_eth_mac_tx(tfw_eth_mac_t *m, const void *frame, size_t len)
{
    const uint8_t *f = frame;
    static const uint8_t zero[4];

    (void)m;
    if (tx_busy != 0u) {
        if (tx_busy != UINT_MAX) {
            tx_busy--;
        }
        return -1;
    }
    tx_frames++;
    tx_last_len = len < sizeof tx_last ? len : sizeof tx_last;
    memcpy(tx_last, f, tx_last_len);
    ka_tx(f, len);
    if (len >= 42u && f[12] == 0x08u && f[13] == 0x06u && f[21] == 1u &&
        memcmp(f + 28, zero, 4) == 0 && memcmp(f + 38, offered_ip, 4) == 0) {
        arp_probes++;
    }
    /* IPv4, UDP to port 67: BOOTP at 42, its options after the cookie at 282. */
    if (len >= 285u && f[12] == 0x08u && f[13] == 0x00u && f[23] == 17u &&
        f[36] == 0u && f[37] == 67u) {
        memcpy(dhcp_xid, f + 46, 4);
        for (size_t o = 282u; o + 2u < len && f[o] != 255u; o += 2u + f[o + 1u]) {
            if (f[o] == 53u) {
                dhcp_sent |= 1u << f[o + 2u];
                break;
            }
        }
    }
    return 0;
}
int tfw_eth_mac_rx(tfw_eth_mac_t *m, void *out, size_t cap)
{
    size_t n = rx_len;

    (void)m;
    if (n == 0u || n > cap) {
        return 0;
    }
    memcpy(out, rx_frame, n);
    rx_len = 0u;
    return (int)n;
}

/* --- the wire: tested on its own in test_h5_eth_wire.c ------------------- */
static unsigned dial_calls, connects, say_polls;
/* As hal_eth_wire_tcp.c's dial: load the block, reconcile the netif with it,
 * and connect only when the reconcile says the interface is ready. */
void tfw_h5_wire_dial_poll(void)
{
    tfw_netcfg_t cfg;

    dial_calls++;
    if (tfw_netcfg_load(&cfg) == TFW_OK && tfw_h5_eth_reconcile(&cfg)) {
        connects++;
    }
}
static unsigned bound_says;
static char bound_line[48];
static unsigned phy_none_says, mac_reset_says;
void tfw_h5_wire_say(const char *s)
{
    if (strcmp(s, "eth: phy none\r\n") == 0) {
        phy_none_says++;
    }
    if (strcmp(s, "eth: mac reset timeout\r\n") == 0) {
        mac_reset_says++;
    }
    if (strncmp(s, "eth: dhcp bound", 15u) == 0) {
        bound_says++;
        strncpy(bound_line, s, sizeof bound_line - 1u);
    }
}
void tfw_h5_wire_say_poll(void) { say_polls++; }

/* --- the network block in EEPROM (#584, Codex round 4, ruling 989) -------- */
static uint8_t eeprom_block[TFW_NETCFG_LEN];
static uint8_t eeprom_fails;

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if (eeprom_fails || addr != TFW_NETCFG_ADDR || len > sizeof eeprom_block) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    memcpy(buf, eeprom_block, len);
    return TFW_OK;
}

/* The block's addresses: RFC 5737; its MACs: 02:00:00 test MACs. */
static uint8_t block_host = 20u;        /* the block's own address: 192.0.2.<this> */

static void put_block(uint8_t flags, const uint8_t mac[6])
{
    uint8_t addrs[18] = {
        192u, 0u, 2u, 20u, 255u, 255u, 255u, 0u, 192u, 0u, 2u, 1u,
        192u, 0u, 2u, 50u, 0x0Fu, 0xA0u,        /* endpoint, port 4000 */
    };
    uint16_t crc;

    addrs[3] = block_host;
    memcpy(eeprom_block, "TFNC", 4);
    eeprom_block[4] = TFW_NETCFG_VERSION;
    eeprom_block[5] = flags;
    memcpy(eeprom_block + 6, mac, 6);
    memcpy(eeprom_block + 12, addrs, sizeof addrs);
    crc = tfw_crc16(eeprom_block, 30u);
    eeprom_block[30] = (uint8_t)crc;
    eeprom_block[31] = (uint8_t)(crc >> 8);
}

static const uint8_t no_mac[6];

void setUp(void) { }
void tearDown(void) { }

/* Each netcfg case brings a fresh netif up: it runs in a child, before any
 * test in the parent has brought its own netif up. */
static void good_seed(void);

static int in_child(void (*fn)(void))
{
    pid_t pid;
    int st = 0;

    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        good_seed();
        if (TEST_PROTECT()) {
            fn();
        }
        fflush(stdout);
        _exit(Unity.CurrentTestFailed ? 1 : 0);
    }
    TEST_ASSERT_TRUE(pid > 0);
    TEST_ASSERT_EQUAL_INT(pid, waitpid(pid, &st, 0));
    return WIFEXITED(st) ? WEXITSTATUS(st) : 255;
}

static void run_for(tfw_ms_t ms);

static void static_block(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u };
    struct netif *n;

    put_block(0u, mac);
    link_up_flag = 1u;
    run_for(5000u);
    n = netif_default;
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(mac_running);
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(LWIP_MAKEU32(192u, 0u, 2u, 20u)), ip4_addr_get_u32(netif_ip4_addr(n)));
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(LWIP_MAKEU32(255u, 255u, 255u, 0u)), ip4_addr_get_u32(netif_ip4_netmask(n)));
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(LWIP_MAKEU32(192u, 0u, 2u, 1u)), ip4_addr_get_u32(netif_ip4_gw(n)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, n->hwaddr, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, mac_seen, 6);
    TEST_ASSERT_NULL(netif_dhcp_data(n));               /* DHCP never started */
    TEST_ASSERT_EQUAL_UINT(0u, dhcp_sent);
    TEST_ASSERT_TRUE(dial_calls > 0u);                  /* the wire dials out */
}

static void test_a_static_block_sets_its_address_and_mac_and_no_dhcp(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(static_block));
}

/* A PBUF_RAM pbuf of len bytes: an Ethernet header with the local
 * experimental ethertype 0x88B5 (so ka_tx and the ARP/DHCP decode ignore it),
 * then bytes counting up from seed. */
static struct pbuf *raw_pbuf(u16_t len, uint8_t seed)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
    uint8_t *b;

    TEST_ASSERT_NOT_NULL(p);
    b = p->payload;
    for (u16_t i = 0u; i < len; i++) {
        b[i] = (uint8_t)(seed + i);
    }
    if (len >= 14u) {
        b[12] = 0x88u;
        b[13] = 0xB5u;
    }
    return p;
}

static void linkoutput_cases(void)
{
    struct netif *n;
    struct pbuf *head, *tail, *p;
    uint8_t want[100];
    unsigned before;

    static_block();
    n = netif_default;

    /* (a) a chain of two pbufs goes out as one frame, both parts in order */
    head = raw_pbuf(60u, 0x10u);
    tail = raw_pbuf(40u, 0xA0u);
    memcpy(want, head->payload, 60u);
    memcpy(want + 60, tail->payload, 40u);
    pbuf_cat(head, tail);
    TEST_ASSERT_EQUAL_UINT(100u, head->tot_len);
    tx_last_len = 0u;
    before = tx_frames;
    TEST_ASSERT_EQUAL_INT(ERR_OK, n->linkoutput(n, head));
    TEST_ASSERT_EQUAL_UINT(before + 1u, tx_frames);
    TEST_ASSERT_EQUAL_UINT(100u, tx_last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, tx_last, 100);
    pbuf_free(head);

    /* (b) one byte over the frame buffer is refused and nothing is sent */
    p = raw_pbuf((u16_t)(TFW_ETH_BUF_SIZE + 1u), 0x20u);
    before = tx_frames;
    TEST_ASSERT_EQUAL_INT(ERR_BUF, n->linkoutput(n, p));
    TEST_ASSERT_EQUAL_UINT(before, tx_frames);
    pbuf_free(p);

    /* (c) exactly the frame buffer's size is sent */
    p = raw_pbuf((u16_t)TFW_ETH_BUF_SIZE, 0x30u);
    tx_last_len = 0u;
    TEST_ASSERT_EQUAL_INT(ERR_OK, n->linkoutput(n, p));
    TEST_ASSERT_EQUAL_UINT(TFW_ETH_BUF_SIZE, tx_last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p->payload, tx_last, TFW_ETH_BUF_SIZE);
    pbuf_free(p);

    /* (d) a ring full for a few tries still sends the frame once */
    p = raw_pbuf(64u, 0x40u);
    tx_busy = 3u;
    before = tx_frames;
    TEST_ASSERT_EQUAL_INT(ERR_OK, n->linkoutput(n, p));
    TEST_ASSERT_EQUAL_UINT(0u, tx_busy);
    TEST_ASSERT_EQUAL_UINT(before + 1u, tx_frames);

    /* (e) a ring that never drains ends in an error, not a hang */
    tx_busy = UINT_MAX;
    before = tx_frames;
    TEST_ASSERT_EQUAL_INT(ERR_IF, n->linkoutput(n, p));
    TEST_ASSERT_EQUAL_UINT(before, tx_frames);
    tx_busy = 0u;
    pbuf_free(p);
}

static void test_the_netif_link_output_refuses_flattens_and_bounds_the_send(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(linkoutput_cases));
}

static void dhcp_block(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x02u };

    put_block(TFW_NETCFG_DHCP, mac);
    link_up_flag = 1u;
    run_for(5000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_TRUE((dhcp_sent & (1u << DHCP_DISCOVER)) != 0u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, netif_default->hwaddr, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, mac_seen, 6);
    TEST_ASSERT_EQUAL_UINT(0u, connects);               /* no lease yet */
}

static void test_a_dhcp_block_starts_dhcp_with_the_block_mac(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(dhcp_block));
}

/* The DeviceID the ATTEST evidence carries (src/caps/attest.c): the 16-byte
 * platform serial, here 00 01 .. 0f. */
const uint8_t *tfw_platform_id_serial(void)
{
    static const uint8_t serial[16] = {
        0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u,
        0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu,
    };
    return serial;
}

static void zero_mac_block(void)
{
    /* #584 Codex P1: sdk/go DerivedMAC of that serial, the vector
     * sdk/go/netcfg_test.go's TestDerivedMACIsPinned pins. */
    static const uint8_t want[6] = { 0x86u, 0xC4u, 0x10u, 0x37u, 0x0Au, 0x0Au };

    put_block(TFW_NETCFG_DHCP, no_mac);
    link_up_flag = 1u;
    run_for(5000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, netif_default->hwaddr, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, mac_seen, 6);
}

/* docs/SPEC.md (ethernet-transport) and the H5 Ethernet ADR: a locally
 * administered address derived on the device until a MAC is supplied. */
static void test_an_all_zero_block_mac_takes_the_deviceid_mac(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(zero_mac_block));
}

static void unprovisioned(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x03u };
    tfw_netcfg_t cfg;

    put_block(0u, mac);
    eeprom_block[31] ^= 0xFFu;                          /* bad CRC */
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_netcfg_load(&cfg));
    link_up_flag = 1u;
    say_polls = 0u;
    run_for(30000u);
    TEST_ASSERT_NULL(netif_default);                    /* off the network */
    TEST_ASSERT_EQUAL_UINT(0u, tx_frames);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_EQUAL_UINT(0u, dial_calls);
    TEST_ASSERT_EQUAL_UINT(300u, say_polls);            /* the UART is served */

    eeprom_fails = 1u;                                  /* no block at all */
    run_for(30000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_EQUAL_UINT(0u, tx_frames);

    eeprom_fails = 0u;                                  /* written later: */
    put_block(0u, mac);                                 /* never cached */
    run_for(30000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_TRUE(dial_calls > 0u);
}

static void test_a_missing_or_bad_block_keeps_the_device_off_the_network(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(unprovisioned));
}

static void run_for(tfw_ms_t ms)
{
    for (tfw_ms_t end = now_ms + ms; now_ms < end; now_ms += 100u) {
        tfw_hal_eth_poll();
    }
}

/* The lease, as dhcp_bind leaves it after an ACK: 3 coarse ticks (180 s),
 * renewing at 60 s and rebinding at 120 s. */
/* A DHCP reply from the server at 192.0.2.1 (MAC 02:00:00:00:00:01) to the
 * broadcast address, for the client's last xid, offering offered_ip. */
static void inject_dhcp_reply(uint8_t type)
{
    static const uint8_t opts[] = {
        53, 1, 0,                       /* message type, patched below */
        54, 4, 192, 0, 2, 1,            /* server identifier */
        51, 4, 0, 0, 0x0e, 0x10,        /* lease 3600 s */
        1, 4, 255, 255, 255, 0,         /* subnet mask */
        255
    };
    uint8_t *f = rx_frame, *ip = f + 14, *udp = f + 34, *b = f + 42;
    uint32_t sum = 0u;

    memset(rx_frame, 0, sizeof rx_frame);
    memset(f, 0xFF, 6);
    f[6] = 0x02u; f[11] = 0x01u;
    f[12] = 0x08u;
    ip[0] = 0x45u; ip[2] = 0x01u; ip[3] = 0x48u;   /* 20 + 8 + 300 */
    ip[8] = 64u; ip[9] = 17u;
    ip[12] = 192u; ip[13] = 0u; ip[14] = 2u; ip[15] = 1u;
    memset(ip + 16, 0xFF, 4);
    for (unsigned i = 0u; i < 20u; i += 2u) {
        sum += ((uint32_t)ip[i] << 8) | ip[i + 1u];
    }
    sum = (sum & 0xFFFFu) + (sum >> 16);
    sum = ~sum & 0xFFFFu;
    ip[10] = (uint8_t)(sum >> 8); ip[11] = (uint8_t)sum;
    udp[1] = 67u; udp[3] = 68u; udp[4] = 0x01u; udp[5] = 0x34u;  /* 8 + 300, no checksum */
    b[0] = 2u; b[1] = 1u; b[2] = 6u;
    memcpy(b + 4, dhcp_xid, 4);
    memcpy(b + 16, offered_ip, 4);
    memcpy(b + 28, netif_default->hwaddr, 6);
    b[236] = 0x63u; b[237] = 0x82u; b[238] = 0x53u; b[239] = 0x63u;
    memcpy(b + 240, opts, sizeof opts);
    b[242] = type;
    rx_len = 42u + 300u;
}

/* An ARP reply from another host (MAC 02:00:00:00:00:02) that holds offered_ip. */
static void inject_arp_conflict(void)
{
    uint8_t *f = rx_frame, *a = f + 14;

    memset(rx_frame, 0, sizeof rx_frame);
    memset(f, 0xFF, 6);
    f[6] = 0x02u; f[11] = 0x02u;
    f[12] = 0x08u; f[13] = 0x06u;
    a[1] = 1u; a[2] = 0x08u; a[4] = 6u; a[5] = 4u; a[7] = 2u;
    a[8] = 0x02u; a[13] = 0x02u;
    memcpy(a + 14, offered_ip, 4);
    memcpy(a + 24, offered_ip, 4);
    rx_len = 60u;
}

static int run_until(const unsigned *what, unsigned want, tfw_ms_t max_ms)
{
    for (tfw_ms_t end = now_ms + max_ms; now_ms < end; now_ms += 100u) {
        tfw_hal_eth_poll();
        if ((*what & want) != 0u) {
            return 1;
        }
    }
    return 0;
}

/* DISCOVER, OFFER, REQUEST, ACK: the client then probes the offered address
 * (RFC 5227) before taking it. */
static void offer_and_ack(void)
{
    TEST_ASSERT_TRUE(run_until(&dhcp_sent, 1u << DHCP_DISCOVER, 70000u));
    inject_dhcp_reply(DHCP_OFFER);
    TEST_ASSERT_TRUE(run_until(&dhcp_sent, 1u << DHCP_REQUEST, 2000u));
    arp_probes = 0u;
    inject_dhcp_reply(DHCP_ACK);
    run_for(100u);
}

/* #584 Codex round 3: another host answering the probe for the offered
 * address makes the client decline it, not bind it. */
static void test_a_conflicting_arp_reply_declines_the_offered_address(void)
{
    link_up_flag = 1u;
    dhcp_sent = 0u;
    offer_and_ack();
    TEST_ASSERT_FALSE(dhcp_supplied_address(netif_default));
    TEST_ASSERT_TRUE(run_until(&arp_probes, ~0u, 3000u));
    inject_arp_conflict();
    run_for(500u);
    TEST_ASSERT_TRUE((dhcp_sent & (1u << DHCP_DECLINE)) != 0u);
    run_for(10000u);
    TEST_ASSERT_FALSE(dhcp_supplied_address(netif_default));
    TEST_ASSERT_TRUE(ip4_addr_isany_val(*netif_ip4_addr(netif_default)));
}

/* With no reply to the probes, the client binds the offered address. */
static void test_an_unanswered_probe_binds_the_offered_address(void)
{
    dhcp_sent = 0u;
    offer_and_ack();
    run_for(10000u);
    TEST_ASSERT_TRUE(arp_probes >= 3u);
    TEST_ASSERT_TRUE((dhcp_sent & (1u << DHCP_DECLINE)) == 0u);
    TEST_ASSERT_TRUE(dhcp_supplied_address(netif_default));
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(0xC000020Au), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
}

/* hal_eth_netif.c lines 11-12 and 500-503: the bind above says "eth: dhcp
 * bound" and the leased address once, and later polls say it no more. */
static void test_the_console_says_the_bound_address_once(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, bound_says);
    TEST_ASSERT_EQUAL_STRING("eth: dhcp bound 192.0.2.10\r\n", bound_line);
    run_for(5000u);
    TEST_ASSERT_EQUAL_UINT(1u, bound_says);
}

static void bind_by_hand(struct netif *nif)
{
    struct dhcp *d = netif_dhcp_data(nif);
    ip4_addr_t ip, mask, gw;

    IP4_ADDR(&ip, 192, 0, 2, 10);       /* RFC 5737 documentation range */
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 0, 2, 1);
    d->state = DHCP_STATE_BOUND;
    d->t0_timeout = 3u;
    d->t1_timeout = 1u;
    d->t2_timeout = 2u;
    d->t1_renew_time = 1u;
    d->t2_rebind_time = 2u;
    d->lease_used = 0u;
    netif_set_addr(nif, &ip, &mask, &gw);
}

static void test_a_link_flap_keeps_the_lease_clock_and_an_expired_lease_clears(void)
{
    struct netif *nif;

    link_up_flag = 1u;
    run_for(1000u);
    nif = netif_default;
    TEST_ASSERT_NOT_NULL(nif);
    TEST_ASSERT_NOT_NULL(netif_dhcp_data(nif));
    bind_by_hand(nif);
    TEST_ASSERT_TRUE(dhcp_supplied_address(nif));

    run_for(30000u);                    /* 30 s into the lease */
    link_up_flag = 0u;
    run_for(1000u);
    TEST_ASSERT_FALSE(netif_is_link_up(nif));
    link_up_flag = 1u;
    tx_frames = 0u;
    run_for(1000u);
    TEST_ASSERT_TRUE(netif_is_link_up(nif));
    TEST_ASSERT_TRUE(tx_frames > 0u);   /* the lease is asked after, not restarted */
    TEST_ASSERT_EQUAL_UINT16(3u, netif_dhcp_data(nif)->t0_timeout);

    run_for(60000u);                    /* about 95 s in: the lease still holds */
    TEST_ASSERT_EQUAL_HEX32(PP_HTONL(0xC000020Au), ip4_addr_get_u32(netif_ip4_addr(nif)));
    run_for(140000u);                   /* past the 180 s lease, no reply */
    TEST_ASSERT_TRUE(ip4_addr_isany_val(*netif_ip4_addr(nif)));
    TEST_ASSERT_FALSE(dhcp_supplied_address(nif));
}

static void test_a_bad_pscsr_leaves_the_mac_stopped_until_a_good_read(void)
{
    struct netif *nif;

    link_up_flag = 0u;
    run_for(1000u);
    TEST_ASSERT_FALSE(mac_running);

    pscsr_fail = 1u;                    /* the read fails */
    link_up_flag = 1u;
    run_for(1000u);
    TEST_ASSERT_FALSE(mac_running);
    nif = netif_default;
    TEST_ASSERT_NOT_NULL(nif);
    TEST_ASSERT_FALSE(netif_is_link_up(nif));

    pscsr_fail = 0u;                    /* read, but no speed in it */
    pscsr_val = 0u;
    run_for(1000u);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_FALSE(netif_is_link_up(nif));

    pscsr_val = 0x0014u;                /* 10 Mbit/s full duplex */
    run_for(1000u);
    TEST_ASSERT_TRUE(mac_running);
    TEST_ASSERT_TRUE(netif_is_link_up(nif));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_DM, mac_cr);
    pscsr_val = 0x0018u;
}

/* The RNG's registers, faked: RNG_CR, RNG_SR, RNG_DR; RCC_CR, RCC_AHB2ENR. */
static volatile uint32_t fake_rng[3];
static volatile uint32_t fake_rcc_cr;
static volatile uint32_t fake_rcc_ahb2enr;
#define RNG_WORD 0x5eed1234u

static void good_seed(void)
{
    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_DRDY;
    fake_rng[TFW_H5_RNG_DR_IX] = RNG_WORD;
}

static unsigned bits_set(uint32_t v)
{
    unsigned n = 0u;

    for (; v != 0u; v &= v - 1u) {
        n++;
    }
    return n;
}

/* A seed error that does not clear, or a zero word, keeps the netif down (no
 * lwIP, no MAC, no PHY scan). */
static void test_an_rng_seed_error_leaves_the_netif_down(void)
{
    link_up_flag = 0u;
    /* A seed error that persists: the driver pulses CONDRST and clears the
     * latch, SECS stays set, the netif stays down. */
    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_SECS | TFW_H5_RNG_SR_SEIS |
                                 TFW_H5_RNG_SR_DRDY;
    fake_rng[TFW_H5_RNG_DR_IX] = RNG_WORD;
    run_for(3000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rng[TFW_H5_RNG_SR_IX] & TFW_H5_RNG_SR_SEIS);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rng[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST);

    /* A latched clock error clears; a zero word still keeps it down. */
    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_CEIS | TFW_H5_RNG_SR_DRDY;
    fake_rng[TFW_H5_RNG_DR_IX] = 0u;    /* a zero word is the seed-error output */
    run_for(1000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rng[TFW_H5_RNG_SR_IX] & TFW_H5_RNG_SR_CEIS);
}

/* RM0481: SEIS stays set after a transient seed error has passed until the
 * driver writes 0 to it. Only the driver clears it here; the next retry
 * recovers and brings the netif up with a key drawn after the recovery. */
static void test_a_transient_rng_seed_error_recovers_on_the_next_retry(void)
{
    link_up_flag = 0u;
    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_SEIS | TFW_H5_RNG_SR_DRDY;
    fake_rng[TFW_H5_RNG_DR_IX] = RNG_WORD;
    run_for(1000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_RNG_SR_DRDY, fake_rng[TFW_H5_RNG_SR_IX]);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rng[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_CONDRST);
    TEST_ASSERT_TRUE((fake_rcc_cr & TFW_H5_RCC_CR_HSI48ON) != 0u);
    TEST_ASSERT_TRUE((fake_rcc_ahb2enr & TFW_H5_RCC_AHB2ENR_RNGEN) != 0u);
    TEST_ASSERT_TRUE((fake_rng[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN) != 0u);
}

/* HSI48 never readies: the RNG clock and the RNG stay off and the netif stays
 * down, with no fallback key. The next retry, with HSI48 ready, brings it up. */
static void hsi48_never_ready(void)
{
    fake_rcc_cr = 0u;
    fake_rcc_ahb2enr = 0u;
    fake_rng[TFW_H5_RNG_CR_IX] = 0u;
    link_up_flag = 0u;
    run_for(1000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_TRUE((fake_rcc_cr & TFW_H5_RCC_CR_HSI48ON) != 0u);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rcc_ahb2enr & TFW_H5_RCC_AHB2ENR_RNGEN);
    TEST_ASSERT_EQUAL_HEX32(0u, fake_rng[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN);

    fake_rcc_cr |= TFW_H5_RCC_CR_HSI48RDY;
    run_for(2000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_TRUE((fake_rcc_ahb2enr & TFW_H5_RCC_AHB2ENR_RNGEN) != 0u);
    TEST_ASSERT_TRUE((fake_rng[TFW_H5_RNG_CR_IX] & TFW_H5_RNG_CR_RNGEN) != 0u);
}

static void test_an_hsi48_that_never_readies_keeps_the_rng_off_and_the_netif_down(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(hsi48_never_ready));
}

/* A missing PHY or a MAC stuck in reset is terminal for the boot: unlike an
 * RNG or netcfg refusal, bring-up is not retried, lwIP is never brought up
 * and never polled, and nothing is sent or dialled. */
static void bringup_fails_for_the_boot(uint8_t *flag, const unsigned *says)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u };

    put_block(0u, mac);
    link_up_flag = 1u;
    *flag = 1u;
    run_for(1000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_EQUAL_UINT(0u, tx_frames);
    TEST_ASSERT_EQUAL_UINT(0u, dial_calls);
    TEST_ASSERT_EQUAL_UINT(1u, *says);

    tfw_h5_eth_service_deadlines();       /* lwIP untouched while failed */
    TEST_ASSERT_NULL(netif_default);

    *flag = 0u;                           /* the fault clears: still no retry */
    run_for(2000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);
    TEST_ASSERT_EQUAL_UINT(0u, tx_frames);
    TEST_ASSERT_EQUAL_UINT(0u, dial_calls);
    TEST_ASSERT_EQUAL_UINT(1u, *says);
}

static void phy_absent_fails_for_the_boot(void)
{
    bringup_fails_for_the_boot(&phy_absent, &phy_none_says);
    TEST_ASSERT_EQUAL_UINT(0u, mac_reset_says);
}

static void mac_reset_timeout_fails_for_the_boot(void)
{
    bringup_fails_for_the_boot(&mac_reset_fail, &mac_reset_says);
    TEST_ASSERT_EQUAL_UINT(0u, phy_none_says);
}

static void test_no_phy_keeps_the_netif_down_for_the_boot(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(phy_absent_fails_for_the_boot));
}

static void test_a_mac_reset_timeout_keeps_the_netif_down_for_the_boot(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(mac_reset_timeout_fails_for_the_boot));
}

/* The driver alone, on its own fake registers (hal_rng.c lines 33-105): an
 * error that does not clear fails init and read, a clock error with no latch
 * is still an error, no DRDY is no word, and a refused read leaves *out. */
static void test_the_rng_driver_reports_an_error_that_stays_and_no_ready_word(void)
{
    volatile uint32_t regs[3] = { 0u, 0u, RNG_WORD };
    volatile uint32_t rcc_cr = TFW_H5_RCC_CR_HSI48RDY;
    volatile uint32_t rcc_ahb2enr = 0u;
    const tfw_h5_rng_t r = { regs, &rcc_cr, &rcc_ahb2enr };
    uint32_t out = 0xA5A5A5A5u;

    regs[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_CECS | TFW_H5_RNG_SR_CEIS |
                             TFW_H5_RNG_SR_DRDY;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_rng_init(&r));

    regs[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_SECS | TFW_H5_RNG_SR_SEIS |
                             TFW_H5_RNG_SR_DRDY;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_rng_init(&r));

    regs[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_CECS | TFW_H5_RNG_SR_DRDY;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_rng_read(&r, &out));

    regs[TFW_H5_RNG_SR_IX] = 0u;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_rng_read(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, out);

    regs[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_DRDY;
    TEST_ASSERT_EQUAL_INT(0, tfw_h5_rng_init(&r));
    TEST_ASSERT_EQUAL_INT(0, tfw_h5_rng_read(&r, &out));
    TEST_ASSERT_EQUAL_HEX32(RNG_WORD, out);
}

/* RFC 6528: two listeners (local ports) and two keys on one 4-tuple at one M
 * give unrelated ISNs; the same tuple and key at a later M advances by M. The
 * hook lwIP calls uses the key drawn from the RNG at bring-up. */
static void test_tcp_isns_follow_rfc6528(void)
{
    static const uint8_t key_a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const uint8_t key_b[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 };
    uint8_t rng_key[16];
    ip4_addr_t local, remote;
    uint32_t a, b, m = 0x10000000u;

    IP4_ADDR(&local, 192, 0, 2, 10);
    IP4_ADDR(&remote, 198, 51, 100, 7);

    a = tfw_h5_tcp_isn_keyed(key_a, m, &local, 21574u, &remote, 50000u);
    b = tfw_h5_tcp_isn_keyed(key_a, m, &local, 21575u, &remote, 50000u);
    TEST_ASSERT_TRUE(bits_set(a ^ b) >= 8u);
    b = tfw_h5_tcp_isn_keyed(key_b, m, &local, 21574u, &remote, 50000u);
    TEST_ASSERT_TRUE(bits_set(a ^ b) >= 8u);
    b = tfw_h5_tcp_isn_keyed(key_a, m + 250000u, &local, 21574u, &remote, 50000u);
    TEST_ASSERT_EQUAL_HEX32(a + 250000u, b);   /* one second of 4 us ticks */

    for (unsigned i = 0u; i < 4u; i++) {
        rng_key[4u * i] = (uint8_t)(RNG_WORD >> 24);
        rng_key[4u * i + 1u] = (uint8_t)(RNG_WORD >> 16);
        rng_key[4u * i + 2u] = (uint8_t)(RNG_WORD >> 8);
        rng_key[4u * i + 3u] = (uint8_t)RNG_WORD;
    }
    a = tfw_h5_tcp_isn(&local, 21574u, &remote, 50000u);
    b = tfw_h5_tcp_isn_keyed(rng_key, (uint32_t)now_ms * 250u, &local, 21574u, &remote, 50000u);
    TEST_ASSERT_EQUAL_HEX32(b, a);
    b = tfw_h5_tcp_isn(&local, 21575u, &remote, 50000u);
    TEST_ASSERT_TRUE(bits_set(a ^ b) >= 8u);
}

/* --- a block changed after bring-up applies before the next dial (#584) --- */

static const uint8_t mac_a[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Au };
static const uint8_t mac_b[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Bu };

/* What the wire does before it dials: load the block, reconcile the netif. */
static int reconcile_from_block(void)
{
    tfw_netcfg_t cfg;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_netcfg_load(&cfg));
    return tfw_h5_eth_reconcile(&cfg);
}

static uint32_t host_addr(uint8_t host)
{
    return PP_HTONL(LWIP_MAKEU32(192u, 0u, 2u, host));
}

static void static_a_to_static_b(void)
{
    block_host = 20u;
    put_block(0u, mac_a);
    link_up_flag = 1u;
    run_for(5000u);
    TEST_ASSERT_EQUAL_HEX32(host_addr(20u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));

    block_host = 21u;                                   /* written over serial */
    put_block(0u, mac_a);
    TEST_ASSERT_EQUAL_INT(1, reconcile_from_block());   /* dial on the new one */
    TEST_ASSERT_EQUAL_HEX32(host_addr(21u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
    TEST_ASSERT_NULL(netif_dhcp_data(netif_default));
    TEST_ASSERT_TRUE(mac_running);                      /* same MAC: no relink */
}

static void test_a_changed_static_address_applies_before_the_next_dial(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(static_a_to_static_b));
}

static void static_to_dhcp(void)
{
    put_block(0u, mac_a);
    link_up_flag = 1u;
    run_for(5000u);
    dhcp_sent = 0u;

    put_block(TFW_NETCFG_DHCP, mac_a);
    TEST_ASSERT_EQUAL_INT(0, reconcile_from_block());   /* no lease yet: no dial */
    TEST_ASSERT_TRUE(ip4_addr_isany_val(*netif_ip4_addr(netif_default)));
    TEST_ASSERT_NOT_NULL(netif_dhcp_data(netif_default));
    run_for(5000u);
    TEST_ASSERT_TRUE((dhcp_sent & (1u << DHCP_DISCOVER)) != 0u);
}

static void test_a_switch_to_dhcp_drops_the_static_address_and_asks(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(static_to_dhcp));
}

static void dhcp_to_static(void)
{
    struct dhcp *d;

    put_block(TFW_NETCFG_DHCP, mac_a);
    link_up_flag = 1u;
    run_for(5000u);
    bind_by_hand(netif_default);
    TEST_ASSERT_TRUE(dhcp_supplied_address(netif_default));

    block_host = 20u;
    put_block(0u, mac_a);
    TEST_ASSERT_EQUAL_INT(1, reconcile_from_block());
    TEST_ASSERT_EQUAL_HEX32(host_addr(20u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
    d = netif_dhcp_data(netif_default);
    TEST_ASSERT_TRUE(d == NULL || d->state == DHCP_STATE_OFF);
    dhcp_sent = 0u;
    run_for(400000u);                                   /* past the old lease */
    TEST_ASSERT_EQUAL_HEX32(host_addr(20u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
    TEST_ASSERT_EQUAL_UINT(0u, dhcp_sent & (1u << DHCP_DISCOVER));
}

static void test_a_switch_to_static_stops_dhcp_and_keeps_the_block_address(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(dhcp_to_static));
}

static void unbound_dhcp_to_static(void)
{
    put_block(TFW_NETCFG_DHCP, mac_a);
    link_up_flag = 1u;
    run_for(5000u);                                     /* still discovering */

    put_block(0u, mac_a);
    TEST_ASSERT_EQUAL_INT(1, reconcile_from_block());
    run_for(5000u);                                     /* no expiry clears it */
    TEST_ASSERT_EQUAL_HEX32(host_addr(20u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
}

static void test_a_static_address_set_before_any_lease_is_kept(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(unbound_dhcp_to_static));
}

/* With no lease the address is zero; a static block written then must still
 * reach the netif through the poll (the dial poll is the only production
 * caller of the reconcile), and only the connect waits for readiness. */
static void unbound_dhcp_to_static_by_poll(void)
{
    put_block(TFW_NETCFG_DHCP, mac_a);
    link_up_flag = 1u;
    run_for(5000u);                                     /* still discovering */
    TEST_ASSERT_TRUE(ip4_addr_isany_val(*netif_ip4_addr(netif_default)));
    TEST_ASSERT_EQUAL_UINT(0u, connects);               /* not ready: no connect */

    put_block(0u, mac_a);
    run_for(5000u);
    TEST_ASSERT_EQUAL_HEX32(host_addr(20u), ip4_addr_get_u32(netif_ip4_addr(netif_default)));
    TEST_ASSERT_TRUE(connects > 0u);                    /* the dial starts */
}

static void test_a_static_block_written_while_unbound_applies_through_the_poll(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(unbound_dhcp_to_static_by_poll));
}

static void new_mac(void)
{
    put_block(0u, mac_a);
    link_up_flag = 1u;
    run_for(5000u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac_a, mac_seen, 6);

    put_block(0u, mac_b);
    TEST_ASSERT_EQUAL_INT(0, reconcile_from_block());   /* relinks first */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac_b, netif_default->hwaddr, 6);
    TEST_ASSERT_FALSE(mac_running);
    run_for(2000u);
    TEST_ASSERT_TRUE(mac_running);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(mac_b, mac_seen, 6);   /* the MAC filter too */
    TEST_ASSERT_EQUAL_INT(1, reconcile_from_block());
}

static void test_a_new_block_mac_reprograms_the_mac_before_the_next_dial(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(new_mac));
}

/* --- the PHY link is read LINK_POLL_MS (100 ms) after bring-up and after
 * each poll, no sooner and not a period late (#599). run_for steps 100 ms,
 * so these step now_ms by hand to land on either side of the edge. */
static void link_first_poll(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x40u };
    tfw_ms_t t0;

    put_block(0u, mac);
    link_up_flag = 1u;
    t0 = now_ms;
    tfw_hal_eth_poll();                                 /* bring-up */
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);
    now_ms = t0 + 99u;
    tfw_hal_eth_poll();
    TEST_ASSERT_FALSE(mac_running);                     /* not read yet */
    now_ms = t0 + 100u;
    tfw_hal_eth_poll();
    TEST_ASSERT_TRUE(mac_running);                      /* read at 100 ms */
    TEST_ASSERT_TRUE(netif_is_link_up(netif_default));
}

static void test_the_link_is_first_polled_link_poll_ms_after_bring_up(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(link_first_poll));
}

static void link_lost_poll(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x41u };
    tfw_ms_t t0;

    put_block(0u, mac);
    link_up_flag = 1u;
    t0 = now_ms;
    tfw_hal_eth_poll();                                 /* bring-up */
    now_ms = t0 + 100u;
    tfw_hal_eth_poll();
    TEST_ASSERT_TRUE(mac_running);
    link_up_flag = 0u;
    now_ms = t0 + 199u;
    tfw_hal_eth_poll();
    TEST_ASSERT_TRUE(mac_running);                      /* not read yet */
    TEST_ASSERT_TRUE(netif_is_link_up(netif_default));
    now_ms = t0 + 200u;
    tfw_hal_eth_poll();
    TEST_ASSERT_FALSE(mac_running);                     /* read at +100 ms */
    TEST_ASSERT_FALSE(netif_is_link_up(netif_default));
}

static void test_a_lost_link_is_seen_link_poll_ms_after_the_last_poll(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(link_lost_poll));
}

/* --- BMSR's link bit latches low: the link is the second of two reads (#599) */
static void bmsr_latch_coming_up(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x42u };
    tfw_ms_t t0;

    put_block(0u, mac);
    link_up_flag = 1u;
    t0 = now_ms;
    tfw_hal_eth_poll();                                 /* bring-up */
    TEST_ASSERT_NOT_NULL(netif_default);
    bmsr_latched_low = 1u;                              /* the first read: 0 */
    now_ms = t0 + 100u;
    tfw_hal_eth_poll();
    TEST_ASSERT_EQUAL_UINT(0u, bmsr_latched_low);
    TEST_ASSERT_TRUE(mac_running);                      /* the second read: up */
    TEST_ASSERT_TRUE(netif_is_link_up(netif_default));
}

static void test_a_link_coming_up_is_taken_from_the_second_bmsr_read(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(bmsr_latch_coming_up));
}

static void bmsr_latch_running(void)
{
    static const uint8_t mac[6] = { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x43u };
    tfw_ms_t t0;

    put_block(0u, mac);
    link_up_flag = 1u;
    t0 = now_ms;
    tfw_hal_eth_poll();                                 /* bring-up */
    now_ms = t0 + 100u;
    tfw_hal_eth_poll();
    TEST_ASSERT_TRUE(mac_running);
    bmsr_latched_low = 1u;                              /* a drop since the last poll */
    now_ms = t0 + 200u;
    tfw_hal_eth_poll();
    TEST_ASSERT_EQUAL_UINT(0u, bmsr_latched_low);
    TEST_ASSERT_TRUE(mac_running);                      /* the second read: still up */
    TEST_ASSERT_TRUE(netif_is_link_up(netif_default));
}

static void test_a_running_link_with_a_latched_low_first_read_stays_up(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(bmsr_latch_running));
}

/* --- the network deadlines run through an ATTEST signature (#584) -------- */

static unsigned sched_polls;

void tfw_sched_poll(void) { sched_polls++; }

static void lease_expires_mid_sign(void)
{
    unsigned dials;

    put_block(TFW_NETCFG_DHCP, mac_a);
    link_up_flag = 1u;
    run_for(1000u);
    bind_by_hand(netif_default);
    run_for(170000u);                                   /* 10 s left on the lease */
    TEST_ASSERT_TRUE(dhcp_supplied_address(netif_default));
    dials = dial_calls;

    /* A 26 s software signature: only the ladder hook's yield runs, so the
     * lwIP timers and the expiry check must run from there. */
    for (tfw_ms_t end = now_ms + 26000u; now_ms < end; now_ms += 100u) {
        tfw_sign_yield();
    }
    TEST_ASSERT_TRUE(sched_polls > 0u);
    TEST_ASSERT_FALSE(dhcp_supplied_address(netif_default));
    TEST_ASSERT_TRUE(ip4_addr_isany_val(*netif_ip4_addr(netif_default)));
    TEST_ASSERT_EQUAL_UINT(dials, dial_calls);          /* nothing dispatched */
}

static void test_the_lease_expires_on_time_during_a_signature(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(lease_expires_mid_sign));
}

static err_t ka_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg; (void)pcb; (void)err;
    if (p != NULL) {
        ka_buffered = (uint16_t)(ka_buffered + p->tot_len);  /* buffered, as the wire's push */
        (void)pbuf_free(p);
    }
    return ERR_OK;
}

static void ka_err(void *arg, err_t err)
{
    (void)arg; (void)err;
    ka_errs++;
    ka_pcb = NULL;
}

static err_t ka_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg; (void)err;
    ka_pcb = pcb;
    tcp_recv(pcb, ka_recv);
    tcp_err(pcb, ka_err);
    ip_set_option(pcb, SOF_KEEPALIVE);   /* the wire's own keepalive */
    pcb->keep_idle  = TFW_H5_WIRE_KEEP_IDLE_MS;
    pcb->keep_intvl = TFW_H5_WIRE_KEEP_INTVL_MS;
    pcb->keep_cnt   = TFW_H5_WIRE_KEEP_CNT;
    return ERR_OK;
}

static void keepalive_answered_mid_sign(void)
{
    static const uint8_t req[4] = { 'R', 'E', 'Q', '!' };
    struct tcp_pcb *l;
    unsigned dials, says;

    static_block();
    l = tcp_new();
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL_INT(ERR_OK, tcp_bind(l, IP_ADDR_ANY, KA_DEV_PORT));
    l = tcp_listen(l);
    TEST_ASSERT_NOT_NULL(l);
    tcp_accept(l, ka_accept);
    ka_inject_arp_request();
    run_for(100u);
    ka_peer_seq = 1000u;
    ka_inject_tcp(0x02u, NULL, 0u);                     /* SYN */
    run_for(100u);
    TEST_ASSERT_TRUE(ka_dev_nxt != 0u);
    ka_peer_seq++;
    ka_inject_tcp(0x10u, NULL, 0u);                     /* ACK */
    run_for(100u);
    TEST_ASSERT_NOT_NULL(ka_pcb);
    ka_inject_tcp(0x18u, req, sizeof req);              /* a request, buffered */
    ka_peer_seq += sizeof req;
    run_for(100u);
    TEST_ASSERT_EQUAL_UINT16(sizeof req, ka_buffered);

    /* 50 ms steps: lwIP re-arms its 250 ms TCP timer from the poll's time, so
     * coarser steps would stretch the keepalive clock. */
    ka_answer = 1u;
    for (tfw_ms_t end = now_ms + 10000u; now_ms < end; now_ms += 50u) {
        tfw_hal_eth_poll();                             /* idle 10 s */
    }
    TEST_ASSERT_EQUAL_UINT(0u, ka_probes);
    dials = dial_calls;
    says  = say_polls;

    /* A 26 s UART signature: the 30 s keepalive deadline falls inside it, and
     * the peer answers every probe, so the connection must survive. */
    for (tfw_ms_t end = now_ms + 26000u; now_ms < end; now_ms += 50u) {
        tfw_sign_yield();
    }
    TEST_ASSERT_TRUE(ka_probes > 0u);
    TEST_ASSERT_EQUAL_UINT(0u, ka_errs);
    TEST_ASSERT_NOT_NULL(ka_pcb);
    TEST_ASSERT_EQUAL_UINT16(sizeof req, ka_buffered);  /* the request is kept */
    TEST_ASSERT_EQUAL_UINT(dials, dial_calls);          /* nothing dispatched */
    TEST_ASSERT_EQUAL_UINT(says, say_polls);

    run_for(100u);                                      /* the main loop again */
    TEST_ASSERT_TRUE(say_polls > says);
    TEST_ASSERT_EQUAL_UINT(0u, ka_errs);
}

static void test_keepalive_answers_are_read_during_a_signature(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(keepalive_answered_mid_sign));
}

int main(void)
{
    tfw_h5_rng.regs = fake_rng;
    tfw_h5_rng.rcc_cr = &fake_rcc_cr;
    tfw_h5_rng.rcc_ahb2enr = &fake_rcc_ahb2enr;
    fake_rcc_cr = TFW_H5_RCC_CR_HSI48RDY;

    put_block(TFW_NETCFG_DHCP, no_mac);     /* the suite's own netif below */

    UNITY_BEGIN();
    RUN_TEST(test_a_static_block_sets_its_address_and_mac_and_no_dhcp);
    RUN_TEST(test_a_dhcp_block_starts_dhcp_with_the_block_mac);
    RUN_TEST(test_an_all_zero_block_mac_takes_the_deviceid_mac);
    RUN_TEST(test_a_missing_or_bad_block_keeps_the_device_off_the_network);
    /* In a child each, before the suite's own netif below comes up. */
    RUN_TEST(test_a_changed_static_address_applies_before_the_next_dial);
    RUN_TEST(test_a_switch_to_dhcp_drops_the_static_address_and_asks);
    RUN_TEST(test_a_switch_to_static_stops_dhcp_and_keeps_the_block_address);
    RUN_TEST(test_a_static_address_set_before_any_lease_is_kept);
    RUN_TEST(test_a_static_block_written_while_unbound_applies_through_the_poll);
    RUN_TEST(test_a_new_block_mac_reprograms_the_mac_before_the_next_dial);
    RUN_TEST(test_the_link_is_first_polled_link_poll_ms_after_bring_up);
    RUN_TEST(test_a_lost_link_is_seen_link_poll_ms_after_the_last_poll);
    RUN_TEST(test_a_link_coming_up_is_taken_from_the_second_bmsr_read);
    RUN_TEST(test_a_running_link_with_a_latched_low_first_read_stays_up);
    RUN_TEST(test_the_lease_expires_on_time_during_a_signature);
    RUN_TEST(test_keepalive_answers_are_read_during_a_signature);
    RUN_TEST(test_the_netif_link_output_refuses_flattens_and_bounds_the_send);
    RUN_TEST(test_an_hsi48_that_never_readies_keeps_the_rng_off_and_the_netif_down);
    RUN_TEST(test_no_phy_keeps_the_netif_down_for_the_boot);
    RUN_TEST(test_a_mac_reset_timeout_keeps_the_netif_down_for_the_boot);
    RUN_TEST(test_the_rng_driver_reports_an_error_that_stays_and_no_ready_word);
    RUN_TEST(test_an_rng_seed_error_leaves_the_netif_down);
    RUN_TEST(test_a_transient_rng_seed_error_recovers_on_the_next_retry);
    RUN_TEST(test_tcp_isns_follow_rfc6528);
    RUN_TEST(test_a_conflicting_arp_reply_declines_the_offered_address);
    RUN_TEST(test_an_unanswered_probe_binds_the_offered_address);
    RUN_TEST(test_the_console_says_the_bound_address_once);
    RUN_TEST(test_a_link_flap_keeps_the_lease_clock_and_an_expired_lease_clears);
    RUN_TEST(test_a_bad_pscsr_leaves_the_mac_stopped_until_a_good_read);
    return UNITY_END();
}
