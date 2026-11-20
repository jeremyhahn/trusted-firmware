/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5's Ethernet under lwIP NO_SYS (#581, the ETH_MAC=1 image): the
 * netif glue between lwIP and hal_eth_mac.c, and the main loop's poll.
 *
 * The poll is a state machine with no wait in it: the first call brings the
 * PHY's pins and clocks up (hal_eth_mdio.c), finds the PHY, resets the MAC
 * and adds the interface; later calls watch the PHY's link every 100 ms,
 * start the MAC at the PHY's speed and DHCP when it comes up, feed received
 * frames to lwIP and run its timers. The console prints "eth: dhcp bound"
 * and the leased address once, when DHCP binds.
 *
 * The rings and buffers are static SRAM, 32-byte aligned. The MAC address is
 * the network block's, or one derived from the DeviceID (eth_block_mac).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tfw/eth.h"
#include "tfw/hal.h"
#include "hal_eth_mac.h"
#include "hal_eth_mdio.h"
#include "hal_eth_wire.h"
#include "hal_rng.h"
#include "../../../crypto/sha256.h"
#include "../../../core/platform_id.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#define ETH_REGS          ((volatile uint32_t *)(uintptr_t)0x40028000u)
#define ETH_RESET_SPIN    1000000u
#define ETH_TX_SPIN       100000u
#define LINK_POLL_MS      100u
#define RNG_RETRY_MS      1000u

enum { ETH_INIT, ETH_WAIT_LINK, ETH_RUNNING, ETH_FAILED };

static tfw_eth_desc_t eth_tx_desc[TFW_ETH_DESC_COUNT] __attribute__((aligned(32)));
static tfw_eth_desc_t eth_rx_desc[TFW_ETH_DESC_COUNT] __attribute__((aligned(32)));
static uint8_t eth_tx_buf[TFW_ETH_DESC_COUNT][TFW_ETH_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t eth_rx_buf[TFW_ETH_DESC_COUNT][TFW_ETH_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t eth_frame[TFW_ETH_BUF_SIZE] __attribute__((aligned(4)));
static tfw_eth_mac_t eth;
static struct netif eth_netif;
static uint8_t eth_addr[6];
static uint8_t eth_state = ETH_INIT;
static uint8_t eth_phy;
static uint8_t eth_link;
static uint8_t eth_bound_said;
static uint8_t eth_dhcp_on;
static uint8_t eth_use_dhcp;            /* the network block's TFW_NETCFG_DHCP */
static uint8_t eth_cfg_failed;          /* no block: bring-up waits RNG_RETRY_MS */
static tfw_ms_t eth_link_ms;
static uint32_t eth_rand_state = 1u;
static uint8_t  eth_isn_key[16];        /* RFC 6528's secret, from the RNG */
static uint8_t  eth_rng_failed;         /* bring-up waits RNG_RETRY_MS */
static tfw_ms_t eth_rng_failed_ms;

uint32_t sys_now(void)
{
    return (uint32_t)tfw_hal_millis();
}

/* xorshift32, seeded from the UID and the boot time: DHCP's xid and TCP's
 * initial sequence numbers, not a secret. */
uint32_t tfw_h5_eth_rand(void)
{
    uint32_t x = eth_rand_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    eth_rand_state = x;
    return x;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* lwIP's LWIP_HOOK_TCP_ISN (lwipopts.h), RFC 6528: ISN = M + F(4-tuple, key),
 * M a 4 us clock (250 ticks a millisecond, wrapping with the millisecond
 * count), F the first 32 bits of SHA-256 over the local address and port, the
 * remote address and port, and the 128-bit key drawn at bring-up. lwIP calls
 * it only after lwip_init, so the key is always drawn by then. */
uint32_t tfw_h5_tcp_isn(const struct ip4_addr *local_ip, uint16_t local_port,
                        const struct ip4_addr *remote_ip, uint16_t remote_port)
{
    return tfw_h5_tcp_isn_keyed(eth_isn_key, (uint32_t)tfw_hal_millis() * 250u,
                                local_ip, local_port, remote_ip, remote_port);
}

/* The hash part, with the key and M given (the host test compares keys). */
uint32_t tfw_h5_tcp_isn_keyed(const uint8_t key[16], uint32_t m,
                              const struct ip4_addr *local_ip, uint16_t local_port,
                              const struct ip4_addr *remote_ip, uint16_t remote_port)
{
    uint8_t msg[12u + 16u];
    uint8_t h[32];

    put_be32(&msg[0], lwip_ntohl(ip4_addr_get_u32(local_ip)));
    msg[4] = (uint8_t)(local_port >> 8);
    msg[5] = (uint8_t)local_port;
    put_be32(&msg[6], lwip_ntohl(ip4_addr_get_u32(remote_ip)));
    msg[10] = (uint8_t)(remote_port >> 8);
    msg[11] = (uint8_t)remote_port;
    memcpy(&msg[12], key, 16u);
    tfw_sha256(msg, (uint16_t)sizeof msg, h);
    memset(msg, 0, sizeof msg);
    return m + (((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) |
                ((uint32_t)h[2] << 8) | (uint32_t)h[3]);
}

/* The ISN key and the LWIP_RAND seed, from the hardware RNG. 0, or -1 if the
 * RNG fails: the netif then stays down, with no fallback key. */
static int eth_draw_secrets(void)
{
    uint32_t w;

    if (tfw_h5_rng_init(&tfw_h5_rng) != 0) {
        return -1;
    }
    for (unsigned i = 0u; i < sizeof eth_isn_key / 4u; i++) {
        if (tfw_h5_rng_read(&tfw_h5_rng, &w) != 0) {
            return -1;
        }
        put_be32(&eth_isn_key[4u * i], w);
    }
    if (tfw_h5_rng_read(&tfw_h5_rng, &w) != 0) {
        return -1;
    }
    eth_rand_state = w | 1u;
    return 0;
}

static void eth_say(const char *s)
{
    tfw_h5_wire_say(s);                 /* between frames only (#584) */
}

static int phy_read(uint8_t reg, uint16_t *v)
{
    return tfw_h5_eth_mdio_read(eth_phy, reg, v);
}

static err_t eth_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint32_t spin = ETH_TX_SPIN;
    u16_t len = p->tot_len;
    int rc;

    (void)nif;
    if (len > sizeof eth_frame) {
        return ERR_BUF;
    }
    (void)pbuf_copy_partial(p, eth_frame, len, 0u);
    while ((rc = tfw_eth_mac_tx(&eth, eth_frame, len)) == -1 && spin-- > 0u) {
    }
    return rc == 0 ? ERR_OK : ERR_IF;
}

static err_t eth_netif_init(struct netif *nif)
{
    nif->name[0] = 'e';
    nif->name[1] = 'n';
    nif->output = etharp_output;
    nif->linkoutput = eth_linkoutput;
    nif->mtu = 1500u;
    nif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(nif->hwaddr, eth_addr, sizeof eth_addr);
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

/* The block's MAC; until a MAC is supplied (an all-zero field), the locally
 * administered one derived from the DeviceID as the SPEC requires: sdk/go's
 * DerivedMAC, the first six bytes of SHA-256("tfw-eth-mac-v1" || id), bit 1
 * of octet 0 set and bit 0 cleared. id is the DeviceID the ATTEST evidence
 * carries (src/caps/attest.c): the 16-byte platform serial, read at boot. */
static void eth_block_mac(const tfw_netcfg_t *cfg, uint8_t mac[6])
{
    static const char domain[] = "tfw-eth-mac-v1";
    static const uint8_t no_mac[6];
    uint8_t msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN];
    uint8_t h[32];

    if (memcmp(cfg->mac, no_mac, sizeof no_mac) != 0) {
        memcpy(mac, cfg->mac, sizeof no_mac);
        return;
    }
    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(&msg[sizeof domain - 1u], tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, h);
    memcpy(mac, h, sizeof no_mac);
    mac[0] = (uint8_t)((mac[0] | 0x02u) & (uint8_t)~0x01u);
}

static void eth_bring_up(void)
{
    tfw_phy_probe_t p;
    tfw_netcfg_t cfg;
    ip4_addr_t ip, mask, gw;

    /* The network block gives the address, mask, gateway and MAC, or asks
     * for DHCP (docs/SPEC.md, ethernet-transport). With none, or a bad one,
     * the device stays off the network and the UART is still served; the
     * block is read again RNG_RETRY_MS on, so one written later is taken. */
    if (tfw_netcfg_load(&cfg) != TFW_OK) {
        if (!eth_cfg_failed) {
            eth_say("eth: not provisioned\r\n");
        }
        eth_cfg_failed = 1u;
        eth_rng_failed_ms = tfw_hal_millis();
        return;                         /* ETH_INIT: tried again later */
    }
    eth_cfg_failed = 0u;
    if (eth_draw_secrets() != 0) {
        if (!eth_rng_failed) {
            eth_say("eth: rng failed\r\n");
        }
        eth_rng_failed = 1u;
        eth_rng_failed_ms = tfw_hal_millis();
        return;                         /* ETH_INIT: tried again later */
    }
    eth_rng_failed = 0u;
    tfw_h5_eth_mdio_init();
    if (tfw_phy_scan(tfw_h5_eth_mdio_read, &p) != 0) {
        eth_say("eth: phy none\r\n");
        eth_state = ETH_FAILED;
        return;
    }
    eth_phy = p.addr;
    eth.regs = ETH_REGS;
    eth.tx = eth_tx_desc;
    eth.rx = eth_rx_desc;
    eth.txbuf = eth_tx_buf;
    eth.rxbuf = eth_rx_buf;
    if (tfw_eth_mac_reset(&eth, ETH_RESET_SPIN) != 0) {
        eth_say("eth: mac reset timeout\r\n");
        eth_state = ETH_FAILED;
        return;
    }
    eth_block_mac(&cfg, eth_addr);
    eth_use_dhcp = (cfg.flags & TFW_NETCFG_DHCP) != 0u;

    lwip_init();
    if (eth_use_dhcp) {
        (void)netif_add(&eth_netif, NULL, NULL, NULL, NULL, eth_netif_init, ethernet_input);
    } else {
        IP4_ADDR(&ip, cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3]);
        IP4_ADDR(&mask, cfg.mask[0], cfg.mask[1], cfg.mask[2], cfg.mask[3]);
        IP4_ADDR(&gw, cfg.gateway[0], cfg.gateway[1], cfg.gateway[2], cfg.gateway[3]);
        (void)netif_add(&eth_netif, &ip, &mask, &gw, NULL, eth_netif_init, ethernet_input);
    }
    netif_set_default(&eth_netif);
    netif_set_up(&eth_netif);
    eth_link_ms = tfw_hal_millis();
    eth_state = ETH_WAIT_LINK;
}

/* BMSR's link bit latches low: the second read is the state. */
static int phy_link(uint16_t *bmsr)
{
    if (phy_read(TFW_PHY_REG_BMSR, bmsr) != 0 || phy_read(TFW_PHY_REG_BMSR, bmsr) != 0) {
        return 0;
    }
    return tfw_phy_link_up(*bmsr);
}

static void eth_watch_link(void)
{
    uint16_t bmsr = 0u, pscsr = 0u;
    uint32_t cr = 0u;
    int up;

    if ((tfw_ms_t)(tfw_hal_millis() - eth_link_ms) < LINK_POLL_MS) {
        return;
    }
    eth_link_ms = tfw_hal_millis();
    up = phy_link(&bmsr);
    if (eth_state == ETH_WAIT_LINK) {
        if (!up || (bmsr & TFW_PHY_BMSR_ANDONE) == 0u) {
            return;
        }
        /* A failed PSCSR read, or a PSCSR naming no speed, leaves the MAC
         * stopped: starting it at a guessed 100 Mbit/s full duplex would
         * mismatch a partner that negotiated otherwise. The next link poll,
         * LINK_POLL_MS on, reads it again. */
        if (phy_read(TFW_PHY_REG_PSCSR, &pscsr) != 0 || !tfw_eth_phy_speed(bmsr, pscsr, &cr)) {
            return;
        }
        tfw_eth_mac_init(&eth, eth_addr, cr);
        tfw_eth_mac_start(&eth);
        eth_link = 1u;
        netif_set_link_up(&eth_netif);
        /* DHCP starts once, at the first link, and only when the network
         * block asks for it; a static block's address is the netif's from
         * bring-up. After a flap, netif_set_link_up has already asked the
         * server to confirm the lease, and the lease timers keep running:
         * restarting here would zero them and leave an expired address in
         * place. */
        if (eth_use_dhcp && !eth_dhcp_on) {
            eth_dhcp_on = 1u;
            (void)dhcp_start(&eth_netif);
        }
        eth_state = ETH_RUNNING;
        return;
    }
    if (!up) {
        /* Link lost: the next partner may negotiate another speed or duplex,
         * so wait for autonegotiation again and re-read PSCSR into MACCR
         * rather than marking the old settings up. */
        tfw_eth_mac_stop(&eth);
        eth_link = 0u;
        eth_bound_said = 0u;
        netif_set_link_down(&eth_netif);
        eth_state = ETH_WAIT_LINK;
    }
}

static void eth_rx_all(void)
{
    for (uint32_t n = 0u; n < TFW_ETH_DESC_COUNT; n++) {
        int len = tfw_eth_mac_rx(&eth, eth_frame, sizeof eth_frame);
        struct pbuf *p;

        if (len == 0) {
            return;
        }
        if (len < 0) {
            continue;
        }
        p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (p == NULL) {
            continue;
        }
        (void)pbuf_take(p, eth_frame, (u16_t)len);
        if (eth_netif.input(p, &eth_netif) != ERR_OK) {
            (void)pbuf_free(p);
        }
    }
}

static void eth_say_bound(void)
{
    char line[40];
    uint8_t a[4];
    uint32_t ip = ip4_addr_get_u32(netif_ip4_addr(&eth_netif));
    size_t n = 0u;
    const char *s = "eth: dhcp bound ";

    memcpy(a, &ip, sizeof a);                     /* network order in memory */
    while (*s != '\0') {
        line[n++] = *s++;
    }
    for (unsigned i = 0u; i < 4u; i++) {
        uint8_t v = a[i];

        if (v >= 100u) {
            line[n++] = (char)('0' + v / 100u);
        }
        if (v >= 10u) {
            line[n++] = (char)('0' + (v / 10u) % 10u);
        }
        line[n++] = (char)('0' + v % 10u);
        line[n++] = i < 3u ? '.' : '\r';
    }
    line[n++] = '\n';
    line[n] = '\0';
    eth_say(line);
}

/* lwIP drops an address at lease expiry only from BOUND, RENEWING or
 * REBINDING. After a link flap whose reboot request went unanswered, DHCP is
 * back to discovering when the lease runs out: dhcp_release_and_stop zeroes the
 * lease clock but leaves the expired address on the netif. With no lease
 * (t0 zero) and no supplied address, any address left is that one. */
static void eth_drop_expired_address(void)
{
    struct dhcp *d = netif_dhcp_data(&eth_netif);

    /* Only while DHCP runs: a static address set after DHCP was stopped is
     * the block's own and never expires. */
    if (eth_dhcp_on && d != NULL && d->t0_timeout == 0u && !dhcp_supplied_address(&eth_netif) &&
        !ip4_addr_isany_val(*netif_ip4_addr(&eth_netif))) {
        netif_set_addr(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        eth_bound_said = 0u;
    }
}

void tfw_h5_eth_service_deadlines(void)
{
    if (eth_state != ETH_WAIT_LINK && eth_state != ETH_RUNNING) {
        return;                         /* no netif yet: lwIP not initialised */
    }
    sys_check_timeouts();
    eth_drop_expired_address();
}

void tfw_h5_eth_service_rx_nodispatch(void)
{
    if (eth_state != ETH_RUNNING) {
        return;                         /* MAC not started: no ring to read */
    }
    eth_rx_all();                       /* into lwIP; the wire's recv only buffers */
}

/* Stops DHCP and forgets the address it gave, if it runs. */
static void eth_stop_dhcp(void)
{
    if (eth_dhcp_on) {
        dhcp_release_and_stop(&eth_netif);
        eth_dhcp_on = 0u;
        eth_bound_said = 0u;
    }
}

int tfw_h5_eth_reconcile(const tfw_netcfg_t *cfg)
{
    uint8_t mac[6];
    uint8_t dhcp = (uint8_t)((cfg->flags & TFW_NETCFG_DHCP) != 0u);
    ip4_addr_t ip, mask, gw;

    if (eth_state != ETH_WAIT_LINK && eth_state != ETH_RUNNING) {
        return 0;
    }
    eth_block_mac(cfg, mac);
    if (memcmp(mac, eth_addr, sizeof eth_addr) != 0) {
        /* A new MAC: a lease was for the old one, and the MAC's address
         * filter is programmed at link up, so the link comes up again. */
        eth_stop_dhcp();
        memcpy(eth_addr, mac, sizeof eth_addr);
        memcpy(eth_netif.hwaddr, mac, sizeof eth_addr);
        if (eth_state == ETH_RUNNING) {
            tfw_eth_mac_stop(&eth);
            eth_link = 0u;
            netif_set_link_down(&eth_netif);
            eth_state = ETH_WAIT_LINK;
        }
    }
    if (dhcp) {
        if (!eth_use_dhcp) {
            netif_set_addr(&eth_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        }
        eth_use_dhcp = 1u;
        if (eth_link && !eth_dhcp_on) { /* otherwise started at link up */
            eth_dhcp_on = 1u;
            (void)dhcp_start(&eth_netif);
        }
    } else {
        eth_stop_dhcp();
        eth_use_dhcp = 0u;
        IP4_ADDR(&ip, cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]);
        IP4_ADDR(&mask, cfg->mask[0], cfg->mask[1], cfg->mask[2], cfg->mask[3]);
        IP4_ADDR(&gw, cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3]);
        netif_set_addr(&eth_netif, &ip, &mask, &gw);
    }
    return eth_state == ETH_RUNNING && eth_link &&
           !ip4_addr_isany_val(*netif_ip4_addr(&eth_netif));
}

void tfw_hal_eth_poll(void)
{
    tfw_h5_wire_say_poll();
    switch (eth_state) {
    case ETH_INIT:
        if ((eth_rng_failed || eth_cfg_failed) &&
            (tfw_ms_t)(tfw_hal_millis() - eth_rng_failed_ms) < RNG_RETRY_MS) {
            return;
        }
        eth_bring_up();
        return;
    case ETH_WAIT_LINK:
        eth_watch_link();
        tfw_h5_eth_service_deadlines();
        return;
    case ETH_RUNNING:
        eth_rx_all();
        eth_watch_link();
        tfw_h5_eth_service_deadlines();
        if (eth_link) {
            /* The device opens the connection. The dial reconciles the netif
             * with the block first, also while it has no address (a static
             * block written with no lease), and connects only once ready. */
            tfw_h5_wire_dial_poll();
        }
        if (!eth_bound_said && dhcp_supplied_address(&eth_netif)) {
            eth_bound_said = 1u;
            eth_say_bound();
        }
        return;
    default:
        return;
    }
}
