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
#include "soc_regs.h"

#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/ip4_addr.h"

void tfw_hal_eth_poll(void);

static tfw_ms_t now_ms;
static uint8_t  link_up_flag;
/* PSCSR: speed field bits 4:2, 0x6 = 100 Mbit/s full duplex; or a failed read. */
static uint16_t pscsr_val = 0x0018u;
static uint8_t  pscsr_fail;
static uint32_t mac_cr;
static uint8_t  mac_running;
static uint8_t  mac_seen[6];            /* the address the MAC was started with */
static unsigned tx_frames;

tfw_ms_t tfw_hal_millis(void) { return now_ms; }

/* --- the PHY on MDIO: present at address 0, link as link_up_flag says ----- */
void tfw_h5_eth_mdio_init(void) { }
int tfw_h5_eth_mdio_read(uint8_t addr, uint8_t reg, uint16_t *val)
{
    if (addr != 0u) {
        *val = 0xFFFFu;
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

int tfw_eth_mac_reset(tfw_eth_mac_t *m, uint32_t spin) { (void)m; (void)spin; return 0; }
void tfw_eth_mac_init(tfw_eth_mac_t *m, const uint8_t mac[6], uint32_t sp) { (void)m; memcpy(mac_seen, mac, 6); mac_cr = sp; }
void tfw_eth_mac_start(tfw_eth_mac_t *m) { (void)m; mac_running = 1u; }
void tfw_eth_mac_stop(tfw_eth_mac_t *m) { (void)m; mac_running = 0u; }
int tfw_eth_mac_tx(tfw_eth_mac_t *m, const void *frame, size_t len)
{
    const uint8_t *f = frame;
    static const uint8_t zero[4];

    (void)m;
    tx_frames++;
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
static unsigned dial_calls, say_polls;
void tfw_h5_wire_dial_poll(void) { dial_calls++; }
void tfw_h5_wire_say(const char *s) { (void)s; }
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
static void put_block(uint8_t flags, const uint8_t mac[6])
{
    static const uint8_t addrs[18] = {
        192u, 0u, 2u, 20u, 255u, 255u, 255u, 0u, 192u, 0u, 2u, 1u,
        192u, 0u, 2u, 50u, 0x0Fu, 0xA0u,        /* endpoint, port 4000 */
    };
    uint16_t crc;

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
static int in_child(void (*fn)(void))
{
    pid_t pid;
    int st = 0;

    fflush(stdout);
    pid = fork();
    if (pid == 0) {
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
    TEST_ASSERT_EQUAL_UINT(0u, dial_calls);             /* no lease yet */
}

static void test_a_dhcp_block_starts_dhcp_with_the_block_mac(void)
{
    TEST_ASSERT_EQUAL_INT(0, in_child(dhcp_block));
}

static void zero_mac_block(void)
{
    uint32_t uid[3];
    uint8_t  want[6];

    for (unsigned i = 0u; i < 3u; i++) {
        uid[i] = 0x01020304u + (uint32_t)i;
    }
    tfw_eth_mac_from_uid(uid, want);
    put_block(TFW_NETCFG_DHCP, no_mac);
    link_up_flag = 1u;
    run_for(5000u);
    TEST_ASSERT_NOT_NULL(netif_default);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, netif_default->hwaddr, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, mac_seen, 6);
}

/* docs/SPEC.md (ethernet-transport) and the H5 Ethernet ADR: a locally
 * administered address derived on the device until a MAC is supplied. */
static void test_an_all_zero_block_mac_takes_the_uid_mac(void)
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
    RUN_TEST(test_an_all_zero_block_mac_takes_the_uid_mac);
    RUN_TEST(test_a_missing_or_bad_block_keeps_the_device_off_the_network);
    RUN_TEST(test_an_rng_seed_error_leaves_the_netif_down);
    RUN_TEST(test_a_transient_rng_seed_error_recovers_on_the_next_retry);
    RUN_TEST(test_tcp_isns_follow_rfc6528);
    RUN_TEST(test_a_conflicting_arp_reply_declines_the_offered_address);
    RUN_TEST(test_an_unanswered_probe_binds_the_offered_address);
    RUN_TEST(test_a_link_flap_keeps_the_lease_clock_and_an_expired_lease_clears);
    RUN_TEST(test_a_bad_pscsr_leaves_the_mac_stopped_until_a_good_read);
    return UNITY_END();
}
