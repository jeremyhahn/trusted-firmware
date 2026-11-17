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
 * locally administered, from the device UID (tfw_eth_mac_from_uid).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "hal_eth_mac.h"
#include "hal_eth_mdio.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#define ETH_REGS          ((volatile uint32_t *)(uintptr_t)0x40028000u)
#define H5_UID_BASE       0x08FFF800u          /* RM0481: 96-bit unique ID */
#define ETH_RESET_SPIN    1000000u
#define ETH_TX_SPIN       100000u
#define LINK_POLL_MS      100u

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
static tfw_ms_t eth_link_ms;
static uint32_t eth_rand_state = 1u;

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

static void eth_say(const char *s)
{
    while (*s != '\0') {
        (void)tfw_hal_uart_tx_try((uint8_t)*s++);
    }
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

static void eth_bring_up(void)
{
    tfw_phy_probe_t p;
    uint32_t uid[3];

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
    for (unsigned i = 0u; i < 3u; i++) {
        uid[i] = *(volatile const uint32_t *)(uintptr_t)(H5_UID_BASE + 4u * i);
    }
    tfw_eth_mac_from_uid(uid, eth_addr);
    eth_rand_state = (uid[0] ^ uid[1] ^ uid[2] ^ (uint32_t)tfw_hal_millis()) | 1u;

    lwip_init();
    (void)netif_add(&eth_netif, NULL, NULL, NULL, NULL, eth_netif_init, ethernet_input);
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
        (void)phy_read(TFW_PHY_REG_PSCSR, &pscsr);
        (void)tfw_eth_phy_speed(bmsr, pscsr, &cr);
        tfw_eth_mac_init(&eth, eth_addr, cr);
        tfw_eth_mac_start(&eth);
        eth_link = 1u;
        netif_set_link_up(&eth_netif);
        (void)dhcp_start(&eth_netif);
        eth_state = ETH_RUNNING;
        return;
    }
    if (up && !eth_link) {
        eth_link = 1u;
        netif_set_link_up(&eth_netif);
    } else if (!up && eth_link) {
        eth_link = 0u;
        eth_bound_said = 0u;
        netif_set_link_down(&eth_netif);
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

void tfw_hal_eth_poll(void)
{
    switch (eth_state) {
    case ETH_INIT:
        eth_bring_up();
        return;
    case ETH_WAIT_LINK:
        eth_watch_link();
        sys_check_timeouts();
        return;
    case ETH_RUNNING:
        eth_rx_all();
        eth_watch_link();
        sys_check_timeouts();
        if (!eth_bound_said && dhcp_supplied_address(&eth_netif)) {
            eth_bound_said = 1u;
            eth_say_bound();
        }
        return;
    default:
        return;
    }
}
