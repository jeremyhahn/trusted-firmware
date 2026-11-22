/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 Ethernet MAC/DMA driver (#581) over a fake register block and
 * fake descriptor rings: configuration, the OWN hand-off both ways, ring
 * wrap, the tail pointers and the error paths.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "hal_eth_mac.h"

static uint32_t fake_regs[TFW_ETH_REG_WORDS];
static tfw_eth_desc_t tx[TFW_ETH_DESC_COUNT];
static tfw_eth_desc_t rx[TFW_ETH_DESC_COUNT];
static uint8_t txbuf[TFW_ETH_DESC_COUNT][TFW_ETH_BUF_SIZE];
static uint8_t rxbuf[TFW_ETH_DESC_COUNT][TFW_ETH_BUF_SIZE];
static tfw_eth_mac_t m;
static const uint8_t mac_addr[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

#define R(off) (fake_regs[(off) / 4u])

static uint32_t addr32(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

/* The DMA side: deliver a frame of len bytes into RX descriptor i. */
static void dma_rx(uint32_t i, uint32_t len, uint32_t extra)
{
    for (uint32_t k = 0u; k < len && k < TFW_ETH_BUF_SIZE; k++) {
        rxbuf[i][k] = (uint8_t)(k + i);
    }
    rx[i].des3 = TFW_ETH_DES3_FD | TFW_ETH_DES3_LD | (len & TFW_ETH_DES3_PL) | extra;
}

void setUp(void)
{
    memset(fake_regs, 0, sizeof fake_regs);
    memset(tx, 0, sizeof tx);
    memset(rx, 0, sizeof rx);
    m.regs = fake_regs;
    m.tx = tx;
    m.rx = rx;
    m.txbuf = txbuf;
    m.rxbuf = rxbuf;
    tfw_eth_mac_init(&m, mac_addr, TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM);
}

void tearDown(void) { }

static void test_init_configures_mac_mtl_and_dma(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM | TFW_ETH_MACCR_ACS |
                            TFW_ETH_MACCR_CST, R(TFW_ETH_MACCR));
    TEST_ASSERT_EQUAL_HEX32(0x80005544u, R(TFW_ETH_MACA0HR));
    TEST_ASSERT_EQUAL_HEX32(0x33221102u, R(TFW_ETH_MACA0LR));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MTL_TSF | TFW_ETH_MTL_TXQEN, R(TFW_ETH_MTLTQOMR));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MTL_RSF, R(TFW_ETH_MTLRQOMR));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DMA_PBL32 | (1536u << 1), R(TFW_ETH_DMACRCR));
    TEST_ASSERT_EQUAL_UINT32(3u, R(TFW_ETH_DMACTDRLR));
    TEST_ASSERT_EQUAL_UINT32(3u, R(TFW_ETH_DMACRDRLR));
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[0]), R(TFW_ETH_DMACTDLAR));
    TEST_ASSERT_EQUAL_HEX32(addr32(&rx[0]), R(TFW_ETH_DMACRDLAR));
    TEST_ASSERT_EQUAL_HEX32(addr32(&rx[3]), R(TFW_ETH_DMACRDTPR));
}

static void test_init_hands_every_rx_descriptor_to_the_dma(void)
{
    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX32(addr32(rxbuf[i]), rx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                                rx[i].des3);
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des3);
    }
}

static void test_half_duplex_10m_clears_fes_and_dm(void)
{
    tfw_eth_mac_init(&m, mac_addr, 0u);
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_ACS | TFW_ETH_MACCR_CST, R(TFW_ETH_MACCR));
}

/* hal_eth_netif.c line 305 inits the MAC again at every link-up, over the
 * same rings: hal_eth_mac.c lines 67-81 must zero every TX descriptor and
 * rewind the tail to tx[0], or a slot left owned blocks TX for good. */
static void test_init_again_takes_back_a_used_tx_ring(void)
{
    uint8_t frame[64] = { 0 };

    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, sizeof frame));
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, sizeof frame));
    memset(tx, 0xA5, sizeof tx);
    tfw_eth_mac_init(&m, mac_addr, TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM);
    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des1);
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des2);
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des3);
    }
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[0]), R(TFW_ETH_DMACTDTPR));
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, 42u));
    TEST_ASSERT_EQUAL_UINT32(42u, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[1]), R(TFW_ETH_DMACTDTPR));
}

static void test_start_and_stop(void)
{
    tfw_eth_mac_start(&m);
    TEST_ASSERT_TRUE(R(TFW_ETH_MACCR) & TFW_ETH_MACCR_TE);
    TEST_ASSERT_TRUE(R(TFW_ETH_MACCR) & TFW_ETH_MACCR_RE);
    TEST_ASSERT_TRUE(R(TFW_ETH_DMACTCR) & TFW_ETH_DMA_ST_SR);
    TEST_ASSERT_TRUE(R(TFW_ETH_DMACRCR) & TFW_ETH_DMA_ST_SR);
    tfw_eth_mac_stop(&m);
    TEST_ASSERT_FALSE(R(TFW_ETH_MACCR) & (TFW_ETH_MACCR_TE | TFW_ETH_MACCR_RE));
    TEST_ASSERT_FALSE(R(TFW_ETH_DMACTCR) & TFW_ETH_DMA_ST_SR);
    TEST_ASSERT_FALSE(R(TFW_ETH_DMACRCR) & TFW_ETH_DMA_ST_SR);
    TEST_ASSERT_TRUE(R(TFW_ETH_DMACRCR) & TFW_ETH_DMA_PBL32);
}

static void test_reset_times_out_when_swr_sticks(void)
{
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_reset(&m, 10u));
    TEST_ASSERT_TRUE(R(TFW_ETH_DMAMR) & TFW_ETH_DMAMR_SWR);
}

static void test_tx_fills_the_descriptor_and_moves_the_tail(void)
{
    uint8_t frame[60];

    for (unsigned i = 0u; i < sizeof frame; i++) {
        frame[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, sizeof frame));
    TEST_ASSERT_EQUAL_HEX32(addr32(txbuf[0]), tx[0].des0);
    TEST_ASSERT_EQUAL_UINT32(60u, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_FD | TFW_ETH_DES3_LD | 60u,
                            tx[0].des3);
    TEST_ASSERT_EQUAL_MEMORY(frame, txbuf[0], sizeof frame);
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[1]), R(TFW_ETH_DMACTDTPR));
}

static void test_tx_waits_for_the_dma_and_wraps(void)
{
    uint8_t frame[64] = { 0 };

    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, sizeof frame));
    }
    /* Every descriptor is the DMA's: the fifth frame waits. */
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_tx(&m, frame, sizeof frame));
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[0]), R(TFW_ETH_DMACTDTPR));
    /* The DMA sends the first: it is ours again and the ring wraps to it. */
    tx[0].des3 &= ~TFW_ETH_DES3_OWN;
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, 42u));
    TEST_ASSERT_EQUAL_UINT32(42u, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[1]), R(TFW_ETH_DMACTDTPR));
}

static void test_tx_rejects_empty_and_oversize(void)
{
    static uint8_t big[TFW_ETH_BUF_SIZE + 1u];

    TEST_ASSERT_EQUAL_INT(-2, tfw_eth_mac_tx(&m, big, 0u));
    TEST_ASSERT_EQUAL_INT(-2, tfw_eth_mac_tx(&m, big, sizeof big));
    TEST_ASSERT_EQUAL_HEX32(0u, tx[0].des3);
}

static void test_rx_nothing_while_the_dma_owns_it(void)
{
    uint8_t out[TFW_ETH_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_rx(&m, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT32(0u, m.rx_ix);
}

static void test_rx_copies_rearms_and_wraps(void)
{
    uint8_t out[TFW_ETH_BUF_SIZE];

    for (uint32_t n = 0u; n < TFW_ETH_DESC_COUNT + 2u; n++) {
        uint32_t i = n % TFW_ETH_DESC_COUNT;

        dma_rx(i, 100u + n, 0u);
        TEST_ASSERT_EQUAL_INT((int)(100u + n), tfw_eth_mac_rx(&m, out, sizeof out));
        TEST_ASSERT_EQUAL_MEMORY(rxbuf[i], out, 100u + n);
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                                rx[i].des3);
        TEST_ASSERT_EQUAL_HEX32(addr32(&rx[i]), R(TFW_ETH_DMACRDTPR));
        TEST_ASSERT_EQUAL_UINT32((i + 1u) % TFW_ETH_DESC_COUNT, m.rx_ix);
    }
}

static void test_rx_drops_errors_splits_and_overlong(void)
{
    uint8_t out[64];

    dma_rx(0u, 60u, TFW_ETH_DES3_ES);
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_rx(&m, out, sizeof out));
    TEST_ASSERT_TRUE(rx[0].des3 & TFW_ETH_DES3_OWN);

    dma_rx(1u, 60u, 0u);
    rx[1].des3 &= ~TFW_ETH_DES3_LD;                 /* first of a split frame */
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_rx(&m, out, sizeof out));

    dma_rx(2u, 65u, 0u);                            /* over the caller's cap */
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_rx(&m, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT32(3u, m.rx_ix);
}

static void test_phy_speed_decode(void)
{
    uint32_t cr = 0u;

    TEST_ASSERT_EQUAL_INT(1, tfw_eth_phy_speed(TFW_PHY_BMSR_ANDONE, 6u << 2, &cr));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM, cr);
    TEST_ASSERT_EQUAL_INT(1, tfw_eth_phy_speed(TFW_PHY_BMSR_ANDONE, 2u << 2, &cr));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_FES, cr);
    TEST_ASSERT_EQUAL_INT(1, tfw_eth_phy_speed(TFW_PHY_BMSR_ANDONE, 5u << 2, &cr));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_DM, cr);
    TEST_ASSERT_EQUAL_INT(1, tfw_eth_phy_speed(TFW_PHY_BMSR_ANDONE, 1u << 2, &cr));
    TEST_ASSERT_EQUAL_HEX32(0u, cr);
    /* Autonegotiation not done, or a reserved code: 100M full. */
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_phy_speed(0u, 1u << 2, &cr));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM, cr);
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_phy_speed(TFW_PHY_BMSR_ANDONE, 0u, &cr));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_mac_mtl_and_dma);
    RUN_TEST(test_init_hands_every_rx_descriptor_to_the_dma);
    RUN_TEST(test_half_duplex_10m_clears_fes_and_dm);
    RUN_TEST(test_init_again_takes_back_a_used_tx_ring);
    RUN_TEST(test_start_and_stop);
    RUN_TEST(test_reset_times_out_when_swr_sticks);
    RUN_TEST(test_tx_fills_the_descriptor_and_moves_the_tail);
    RUN_TEST(test_tx_waits_for_the_dma_and_wraps);
    RUN_TEST(test_tx_rejects_empty_and_oversize);
    RUN_TEST(test_rx_nothing_while_the_dma_owns_it);
    RUN_TEST(test_rx_copies_rearms_and_wraps);
    RUN_TEST(test_rx_drops_errors_splits_and_overlong);
    RUN_TEST(test_phy_speed_decode);
    return UNITY_END();
}
