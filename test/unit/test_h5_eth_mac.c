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

/* start clears the DMA status and only adds its enable bits; stop only takes
 * them away: init's speed, duplex, CRC strip and DMA burst setup survive. */
static void check_start_stop_keep(uint32_t speed)
{
    uint32_t cr = R(TFW_ETH_MACCR);
    uint32_t tcr = R(TFW_ETH_DMACTCR);
    uint32_t rcr = R(TFW_ETH_DMACRCR);

    TEST_ASSERT_EQUAL_HEX32(speed | TFW_ETH_MACCR_ACS | TFW_ETH_MACCR_CST, cr);
    R(TFW_ETH_DMACSR) = 0u;
    tfw_eth_mac_start(&m);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, R(TFW_ETH_DMACSR));
    TEST_ASSERT_EQUAL_HEX32(cr | TFW_ETH_MACCR_TE | TFW_ETH_MACCR_RE, R(TFW_ETH_MACCR));
    TEST_ASSERT_EQUAL_HEX32(tcr | TFW_ETH_DMA_ST_SR, R(TFW_ETH_DMACTCR));
    TEST_ASSERT_EQUAL_HEX32(rcr | TFW_ETH_DMA_ST_SR, R(TFW_ETH_DMACRCR));
    tfw_eth_mac_stop(&m);
    TEST_ASSERT_EQUAL_HEX32(cr, R(TFW_ETH_MACCR));
    TEST_ASSERT_EQUAL_HEX32(tcr, R(TFW_ETH_DMACTCR));
    TEST_ASSERT_EQUAL_HEX32(rcr, R(TFW_ETH_DMACRCR));
}

static void test_start_clears_dma_status_and_start_stop_keep_inits_setup(void)
{
    /* setUp ran init at 100M full duplex. */
    check_start_stop_keep(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM);
    /* 10M half duplex: start must not add FES or DM. */
    tfw_eth_mac_init(&m, mac_addr, 0u);
    check_start_stop_keep(0u);
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

/*
 * hal_eth_mac.c tfw_eth_mac_tx refuses a send before it touches the ring:
 * -1 while the next descriptor is still the DMA's, -2 on a zero or
 * oversized length. A refused send must leave the frame the DMA is still
 * sending, its descriptor, the ring index and the tail pointer as they were.
 */
static void test_a_refused_tx_leaves_the_ring_alone(void)
{
    static uint8_t big[TFW_ETH_BUF_SIZE + 1u];
    uint8_t frame[64];
    uint8_t keep_buf[TFW_ETH_BUF_SIZE];
    tfw_eth_desc_t keep_desc;
    uint32_t keep_ix;
    uint32_t keep_tail;

    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        memset(frame, (int)(0x10u + i), sizeof frame);
        TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, sizeof frame));
    }
    memcpy(keep_buf, txbuf[0], sizeof keep_buf);
    keep_desc = tx[0];
    keep_ix = m.tx_ix;
    keep_tail = R(TFW_ETH_DMACTDTPR);

    /* tx[0] is still the DMA's: the send waits and writes nothing. */
    memset(frame, 0x5A, sizeof frame);
    TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_tx(&m, frame, 42u));
    TEST_ASSERT_EQUAL_MEMORY(keep_buf, txbuf[0], sizeof keep_buf);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des0, tx[0].des0);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des1, tx[0].des1);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des2, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des3, tx[0].des3);
    TEST_ASSERT_EQUAL_UINT32(keep_ix, m.tx_ix);
    TEST_ASSERT_EQUAL_HEX32(keep_tail, R(TFW_ETH_DMACTDTPR));

    /* The DMA is done with tx[0], but a bad length is still refused whole. */
    tx[0].des3 &= ~TFW_ETH_DES3_OWN;
    keep_desc = tx[0];
    memset(big, 0xC3, sizeof big);
    TEST_ASSERT_EQUAL_INT(-2, tfw_eth_mac_tx(&m, big, 0u));
    TEST_ASSERT_EQUAL_INT(-2, tfw_eth_mac_tx(&m, big, sizeof big));
    TEST_ASSERT_EQUAL_MEMORY(keep_buf, txbuf[0], sizeof keep_buf);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des0, tx[0].des0);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des1, tx[0].des1);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des2, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(keep_desc.des3, tx[0].des3);
    TEST_ASSERT_EQUAL_UINT32(keep_ix, m.tx_ix);
    TEST_ASSERT_EQUAL_HEX32(keep_tail, R(TFW_ETH_DMACTDTPR));
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

/*
 * A dropped frame copies no byte into the caller's buffer, whatever the
 * DMA wrote into PL, and hands its descriptor straight back to the DMA.
 */
static void test_rx_a_dropped_frame_copies_nothing_and_rearms(void)
{
    static uint8_t out[TFW_ETH_BUF_SIZE + 64u];
    static const struct {
        uint32_t len, extra, clear_ld;
        size_t cap;
    } drop[] = {
        { 0u, 0u, 0u, sizeof out },                     /* zero length */
        { 65u, 0u, 0u, 64u },                           /* over the caller's cap */
        { TFW_ETH_BUF_SIZE + 1u, 0u, 0u, sizeof out },  /* past the DMA buffer */
        { 60u, TFW_ETH_DES3_ES, 0u, sizeof out },       /* error summary */
        { 60u, 0u, 1u, sizeof out },                    /* first of a split frame */
    };

    memset(out, 0xEE, sizeof out);
    for (uint32_t n = 0u; n < sizeof drop / sizeof drop[0]; n++) {
        uint32_t i = n % TFW_ETH_DESC_COUNT;

        dma_rx(i, drop[n].len, drop[n].extra);
        if (drop[n].clear_ld != 0u) {
            rx[i].des3 &= ~TFW_ETH_DES3_LD;
        }
        rx[i].des0 = 0u;                                /* the write-back */
        TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_rx(&m, out, drop[n].cap));
        for (size_t k = 0u; k < sizeof out; k++) {
            TEST_ASSERT_EQUAL_HEX8(0xEEu, out[k]);
        }
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                                rx[i].des3);
        TEST_ASSERT_EQUAL_HEX32(addr32(rxbuf[i]), rx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(addr32(&rx[i]), R(TFW_ETH_DMACRDTPR));
        TEST_ASSERT_EQUAL_UINT32((i + 1u) % TFW_ETH_DESC_COUNT, m.rx_ix);
    }

    /* The next whole frame is copied, and no byte past it. */
    dma_rx(1u, 100u, 0u);
    TEST_ASSERT_EQUAL_INT(100, tfw_eth_mac_rx(&m, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY(rxbuf[1], out, 100u);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[100]);
}

static void test_tx_takes_a_full_buffer_frame(void)
{
    static uint8_t frame[TFW_ETH_BUF_SIZE];

    for (uint32_t i = 0u; i < TFW_ETH_BUF_SIZE; i++) {
        frame[i] = (uint8_t)(i * 3u);
    }
    TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame, TFW_ETH_BUF_SIZE));
    TEST_ASSERT_EQUAL_UINT32(TFW_ETH_BUF_SIZE, tx[0].des2);
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_FD | TFW_ETH_DES3_LD | 1536u,
                            tx[0].des3);
    TEST_ASSERT_EQUAL_MEMORY(frame, txbuf[0], TFW_ETH_BUF_SIZE);
    TEST_ASSERT_EQUAL_HEX32(addr32(&tx[1]), R(TFW_ETH_DMACTDTPR));
}

static void test_rx_takes_a_frame_exactly_the_callers_cap(void)
{
    uint8_t out[65];

    memset(out, 0xEE, sizeof out);
    dma_rx(0u, 64u, 0u);
    TEST_ASSERT_EQUAL_INT(64, tfw_eth_mac_rx(&m, out, 64u));
    TEST_ASSERT_EQUAL_MEMORY(rxbuf[0], out, 64u);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[64]);
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                            rx[0].des3);
    TEST_ASSERT_EQUAL_UINT32(1u, m.rx_ix);
}

static void test_rx_takes_a_full_buffer_frame(void)
{
    static uint8_t out[TFW_ETH_BUF_SIZE + 64u];

    memset(out, 0xEE, sizeof out);
    dma_rx(0u, TFW_ETH_BUF_SIZE, 0u);
    TEST_ASSERT_EQUAL_INT(1536, tfw_eth_mac_rx(&m, out, sizeof out));
    TEST_ASSERT_EQUAL_MEMORY(rxbuf[0], out, TFW_ETH_BUF_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out[TFW_ETH_BUF_SIZE]);
}

static void test_rx_drops_a_split_frames_later_descriptors(void)
{
    static uint8_t out[TFW_ETH_BUF_SIZE];
    static const uint32_t clear[] = {
        TFW_ETH_DES3_FD,                    /* the last descriptor */
        TFW_ETH_DES3_FD | TFW_ETH_DES3_LD,  /* a middle descriptor */
    };

    memset(out, 0xEE, sizeof out);
    for (uint32_t i = 0u; i < sizeof clear / sizeof clear[0]; i++) {
        dma_rx(i, 60u, 0u);
        rx[i].des3 &= ~clear[i];
        rx[i].des0 = 0u;                    /* the write-back */
        TEST_ASSERT_EQUAL_INT(-1, tfw_eth_mac_rx(&m, out, sizeof out));
        for (size_t k = 0u; k < sizeof out; k++) {
            TEST_ASSERT_EQUAL_HEX8(0xEEu, out[k]);
        }
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                                rx[i].des3);
        TEST_ASSERT_EQUAL_HEX32(addr32(rxbuf[i]), rx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(addr32(&rx[i]), R(TFW_ETH_DMACRDTPR));
        TEST_ASSERT_EQUAL_UINT32(i + 1u, m.rx_ix);
    }
}

static void test_init_takes_only_speed_and_duplex_from_its_argument(void)
{
    tfw_eth_mac_init(&m, mac_addr, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM |
                            TFW_ETH_MACCR_ACS | TFW_ETH_MACCR_CST,
                            R(TFW_ETH_MACCR));

    tfw_eth_mac_init(&m, mac_addr, ~(TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_MACCR_ACS | TFW_ETH_MACCR_CST, R(TFW_ETH_MACCR));
}

static void test_init_sets_the_dma_bus_and_tx_burst_and_clears_skip_and_filter(void)
{
    R(TFW_ETH_DMASBMR) = 0xFFFFFFFFu;
    R(TFW_ETH_DMACCR) = 0xFFFFFFFFu;
    R(TFW_ETH_DMACTCR) = 0xFFFFFFFFu;
    R(TFW_ETH_MACPFR) = 0xFFFFFFFFu;

    tfw_eth_mac_init(&m, mac_addr, TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM);

    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DMASBMR_FB | TFW_ETH_DMASBMR_AAL, R(TFW_ETH_DMASBMR));
    TEST_ASSERT_EQUAL_HEX32(0u, R(TFW_ETH_DMACCR));
    TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DMA_PBL32, R(TFW_ETH_DMACTCR));
    TEST_ASSERT_EQUAL_HEX32(0u, R(TFW_ETH_MACPFR));
}

/*
 * hal_eth_mac.c tfw_eth_mac_tx copies each frame into the current slot's
 * own buffer and rewrites DES0 and DES1 over the DMA's write-back: six
 * sends round the four-slot ring land in their own slots, a reused slot
 * loses the write-back, and the wrapped sends leave slots 2 and 3 alone.
 */
static void test_tx_fills_each_slots_own_buffer_across_the_wrap(void)
{
    uint8_t frame[TFW_ETH_DESC_COUNT + 2u][60u + TFW_ETH_DESC_COUNT + 2u];

    for (uint32_t n = 0u; n < TFW_ETH_DESC_COUNT + 2u; n++) {
        uint32_t i = n % TFW_ETH_DESC_COUNT;

        if (n >= TFW_ETH_DESC_COUNT) {
            /* The DMA sent tx[i] and wrote back over it. */
            tx[i].des0 = 0xDEADBEEFu;
            tx[i].des1 = 0xCAFEF00Du;
            tx[i].des3 = TFW_ETH_DES3_LD;
        }
        memset(frame[n], (int)(0x30u + n), sizeof frame[n]);
        TEST_ASSERT_EQUAL_INT(0, tfw_eth_mac_tx(&m, frame[n], 60u + n));
        TEST_ASSERT_EQUAL_HEX32(addr32(txbuf[i]), tx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(0u, tx[i].des1);
        TEST_ASSERT_EQUAL_UINT32(60u + n, tx[i].des2);
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_FD | TFW_ETH_DES3_LD |
                                (60u + n), tx[i].des3);
        TEST_ASSERT_EQUAL_MEMORY(frame[n], txbuf[i], 60u + n);
        TEST_ASSERT_EQUAL_HEX32(addr32(&tx[(i + 1u) % TFW_ETH_DESC_COUNT]),
                                R(TFW_ETH_DMACTDTPR));
    }
    /* The wrapped sends wrote only slots 0 and 1. */
    TEST_ASSERT_EQUAL_MEMORY(frame[2], txbuf[2], 62u);
    TEST_ASSERT_EQUAL_MEMORY(frame[3], txbuf[3], 63u);
}

/*
 * hal_eth_mac.c rx_arm clears DES1 and DES2 before it hands a descriptor
 * back: after a frame, after a dropped frame and at init, the DMA's
 * write-back status words are gone.
 */
static void test_rx_rearm_clears_the_write_back_words(void)
{
    uint8_t out[TFW_ETH_BUF_SIZE];
    static const struct {
        uint32_t extra;
        int want;
    } c[] = {
        { 0u, 60 },                 /* a whole frame */
        { TFW_ETH_DES3_ES, -1 },    /* a dropped frame */
    };

    for (uint32_t i = 0u; i < sizeof c / sizeof c[0]; i++) {
        dma_rx(i, 60u, c[i].extra);
        rx[i].des0 = 0u;                    /* the write-back */
        rx[i].des1 = 0xFFFFFFFFu;
        rx[i].des2 = 0xFFFFFFFFu;
        TEST_ASSERT_EQUAL_INT(c[i].want, tfw_eth_mac_rx(&m, out, sizeof out));
        TEST_ASSERT_EQUAL_HEX32(addr32(rxbuf[i]), rx[i].des0);
        TEST_ASSERT_EQUAL_HEX32(0u, rx[i].des1);
        TEST_ASSERT_EQUAL_HEX32(0u, rx[i].des2);
        TEST_ASSERT_EQUAL_HEX32(TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V,
                                rx[i].des3);
    }

    memset(rx, 0xA5, sizeof rx);
    tfw_eth_mac_init(&m, mac_addr, TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM);
    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX32(0u, rx[i].des1);
        TEST_ASSERT_EQUAL_HEX32(0u, rx[i].des2);
    }
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
    RUN_TEST(test_start_clears_dma_status_and_start_stop_keep_inits_setup);
    RUN_TEST(test_reset_times_out_when_swr_sticks);
    RUN_TEST(test_tx_fills_the_descriptor_and_moves_the_tail);
    RUN_TEST(test_tx_waits_for_the_dma_and_wraps);
    RUN_TEST(test_tx_rejects_empty_and_oversize);
    RUN_TEST(test_a_refused_tx_leaves_the_ring_alone);
    RUN_TEST(test_rx_nothing_while_the_dma_owns_it);
    RUN_TEST(test_rx_copies_rearms_and_wraps);
    RUN_TEST(test_rx_drops_errors_splits_and_overlong);
    RUN_TEST(test_rx_a_dropped_frame_copies_nothing_and_rearms);
    RUN_TEST(test_tx_takes_a_full_buffer_frame);
    RUN_TEST(test_rx_takes_a_frame_exactly_the_callers_cap);
    RUN_TEST(test_rx_takes_a_full_buffer_frame);
    RUN_TEST(test_rx_drops_a_split_frames_later_descriptors);
    RUN_TEST(test_init_takes_only_speed_and_duplex_from_its_argument);
    RUN_TEST(test_init_sets_the_dma_bus_and_tx_burst_and_clears_skip_and_filter);
    RUN_TEST(test_tx_fills_each_slots_own_buffer_across_the_wrap);
    RUN_TEST(test_rx_rearm_clears_the_write_back_words);
    RUN_TEST(test_phy_speed_decode);
    return UNITY_END();
}
