/*
 * The STM32H5 netif (hal_eth_netif.c) on the vendored lwIP, built for the
 * host: the PHY, MAC and wire it drives are fakes here, lwIP's netif, DHCP and
 * timers are the real ones. A link flap must not restart DHCP: after the lease
 * runs out with no reply, the address is cleared (#584, Codex round 3).
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
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

/* --- the MAC: frames sent are counted, none ever arrive (no DHCP server) -- */
int tfw_eth_mac_reset(tfw_eth_mac_t *m, uint32_t spin) { (void)m; (void)spin; return 0; }
void tfw_eth_mac_init(tfw_eth_mac_t *m, const uint8_t mac[6], uint32_t sp) { (void)m; (void)mac; mac_cr = sp; }
void tfw_eth_mac_start(tfw_eth_mac_t *m) { (void)m; mac_running = 1u; }
void tfw_eth_mac_stop(tfw_eth_mac_t *m) { (void)m; mac_running = 0u; }
int tfw_eth_mac_tx(tfw_eth_mac_t *m, const void *frame, size_t len)
{
    (void)m; (void)frame; (void)len;
    tx_frames++;
    return 0;
}
int tfw_eth_mac_rx(tfw_eth_mac_t *m, void *out, size_t cap) { (void)m; (void)out; (void)cap; return 0; }

/* --- the wire: tested on its own in test_h5_eth_wire.c ------------------- */
void tfw_h5_wire_listen(void) { }
void tfw_h5_wire_say(const char *s) { (void)s; }
void tfw_h5_wire_say_poll(void) { }

void setUp(void) { }
void tearDown(void) { }

static void run_for(tfw_ms_t ms)
{
    for (tfw_ms_t end = now_ms + ms; now_ms < end; now_ms += 100u) {
        tfw_hal_eth_poll();
    }
}

/* The lease, as dhcp_bind leaves it after an ACK: 3 coarse ticks (180 s),
 * renewing at 60 s and rebinding at 120 s. */
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

/* A seed error keeps the netif down (no lwIP, no MAC, no PHY scan); once the
 * RNG reads again, the next retry brings it up with HSI48 and RNGEN set. */
static void test_an_rng_seed_error_leaves_the_netif_down(void)
{
    link_up_flag = 0u;
    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_SECS | TFW_H5_RNG_SR_DRDY;
    run_for(3000u);
    TEST_ASSERT_NULL(netif_default);
    TEST_ASSERT_FALSE(mac_running);

    fake_rng[TFW_H5_RNG_SR_IX] = TFW_H5_RNG_SR_DRDY;
    fake_rng[TFW_H5_RNG_DR_IX] = 0u;    /* a zero word is the seed-error output */
    run_for(1000u);
    TEST_ASSERT_NULL(netif_default);

    fake_rng[TFW_H5_RNG_DR_IX] = RNG_WORD;
    run_for(1000u);
    TEST_ASSERT_NOT_NULL(netif_default);
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

    UNITY_BEGIN();
    RUN_TEST(test_an_rng_seed_error_leaves_the_netif_down);
    RUN_TEST(test_tcp_isns_follow_rfc6528);
    RUN_TEST(test_a_link_flap_keeps_the_lease_clock_and_an_expired_lease_clears);
    RUN_TEST(test_a_bad_pscsr_leaves_the_mac_stopped_until_a_good_read);
    return UNITY_END();
}
