/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 Ethernet MAC and DMA driver (#581), poll mode. The register
 * map and the descriptor layout are in hal_eth_mac.h. The rings and their
 * buffers are the caller's (hal_eth_netif.c keeps them in SRAM, 32-byte
 * aligned); the Cortex-M33 has no data cache over SRAM, so a fence before
 * each OWN hand-off is all the ordering the DMA needs.
 */
#include "hal_eth_mac.h"

#define REG(m, off) ((m)->regs[(off) / 4u])

static inline void eth_fence(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint32_t desc_addr(const tfw_eth_desc_t *d)
{
    return (uint32_t)(uintptr_t)d;
}

int tfw_eth_mac_reset(tfw_eth_mac_t *m, uint32_t spin)
{
    REG(m, TFW_ETH_DMAMR) |= TFW_ETH_DMAMR_SWR;
    while ((REG(m, TFW_ETH_DMAMR) & TFW_ETH_DMAMR_SWR) != 0u) {
        if (spin-- == 0u) {
            return -1;
        }
    }
    return 0;
}

static void rx_arm(tfw_eth_mac_t *m, uint32_t i)
{
    tfw_eth_desc_t *d = &m->rx[i];

    d->des0 = (uint32_t)(uintptr_t)m->rxbuf[i];
    d->des1 = 0u;
    d->des2 = 0u;
    eth_fence();
    d->des3 = TFW_ETH_DES3_OWN | TFW_ETH_DES3_IOC | TFW_ETH_DES3_BUF1V;
}

void tfw_eth_mac_init(tfw_eth_mac_t *m, const uint8_t mac[6], uint32_t maccr_speed)
{
    /* DMA: fixed bursts, address-aligned beats, no descriptor skip. */
    REG(m, TFW_ETH_DMASBMR) = TFW_ETH_DMASBMR_FB | TFW_ETH_DMASBMR_AAL;
    REG(m, TFW_ETH_DMACCR) = 0u;
    REG(m, TFW_ETH_DMACTCR) = TFW_ETH_DMA_PBL32;
    REG(m, TFW_ETH_DMACRCR) = TFW_ETH_DMA_PBL32 | (TFW_ETH_BUF_SIZE << 1);

    /* MTL: store and forward both ways, the TX queue enabled. */
    REG(m, TFW_ETH_MTLTQOMR) = TFW_ETH_MTL_TSF | TFW_ETH_MTL_TXQEN;
    REG(m, TFW_ETH_MTLRQOMR) = TFW_ETH_MTL_RSF;

    /* MAC: speed and duplex from the PHY, CRC stripped on receive; the
     * filter passes our unicast address and broadcast (DHCP). */
    REG(m, TFW_ETH_MACCR) = (maccr_speed & (TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM)) |
                            TFW_ETH_MACCR_ACS | TFW_ETH_MACCR_CST;
    REG(m, TFW_ETH_MACPFR) = 0u;
    REG(m, TFW_ETH_MACA0HR) = (1u << 31) | ((uint32_t)mac[5] << 8) | mac[4];
    REG(m, TFW_ETH_MACA0LR) = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                              ((uint32_t)mac[1] << 8) | mac[0];

    /* Rings: TX all ours, RX all the DMA's. */
    for (uint32_t i = 0u; i < TFW_ETH_DESC_COUNT; i++) {
        m->tx[i].des0 = 0u;
        m->tx[i].des1 = 0u;
        m->tx[i].des2 = 0u;
        m->tx[i].des3 = 0u;
        rx_arm(m, i);
    }
    m->tx_ix = 0u;
    m->rx_ix = 0u;
    REG(m, TFW_ETH_DMACTDRLR) = TFW_ETH_DESC_COUNT - 1u;
    REG(m, TFW_ETH_DMACRDRLR) = TFW_ETH_DESC_COUNT - 1u;
    REG(m, TFW_ETH_DMACTDLAR) = desc_addr(&m->tx[0]);
    REG(m, TFW_ETH_DMACRDLAR) = desc_addr(&m->rx[0]);
    REG(m, TFW_ETH_DMACTDTPR) = desc_addr(&m->tx[0]);
    REG(m, TFW_ETH_DMACRDTPR) = desc_addr(&m->rx[TFW_ETH_DESC_COUNT - 1u]);
}

void tfw_eth_mac_start(tfw_eth_mac_t *m)
{
    REG(m, TFW_ETH_DMACSR) = 0xFFFFFFFFu;            /* status is write-1-clear */
    REG(m, TFW_ETH_MACCR) |= TFW_ETH_MACCR_TE;
    REG(m, TFW_ETH_DMACTCR) |= TFW_ETH_DMA_ST_SR;
    REG(m, TFW_ETH_DMACRCR) |= TFW_ETH_DMA_ST_SR;
    REG(m, TFW_ETH_MACCR) |= TFW_ETH_MACCR_RE;
}

void tfw_eth_mac_stop(tfw_eth_mac_t *m)
{
    REG(m, TFW_ETH_DMACTCR) &= ~TFW_ETH_DMA_ST_SR;
    REG(m, TFW_ETH_MACCR) &= ~TFW_ETH_MACCR_RE;
    REG(m, TFW_ETH_DMACRCR) &= ~TFW_ETH_DMA_ST_SR;
    REG(m, TFW_ETH_MACCR) &= ~TFW_ETH_MACCR_TE;
}

int tfw_eth_mac_tx(tfw_eth_mac_t *m, const void *frame, size_t len)
{
    tfw_eth_desc_t *d = &m->tx[m->tx_ix];
    const uint8_t *src = frame;
    uint8_t *dst = m->txbuf[m->tx_ix];

    if (len == 0u || len > TFW_ETH_BUF_SIZE) {
        return -2;
    }
    if ((d->des3 & TFW_ETH_DES3_OWN) != 0u) {
        return -1;
    }
    for (size_t i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
    d->des0 = (uint32_t)(uintptr_t)dst;
    d->des1 = 0u;
    d->des2 = (uint32_t)len;                         /* B1L, no IOC */
    eth_fence();
    d->des3 = TFW_ETH_DES3_OWN | TFW_ETH_DES3_FD | TFW_ETH_DES3_LD | (uint32_t)len;
    eth_fence();
    m->tx_ix = (m->tx_ix + 1u) % TFW_ETH_DESC_COUNT;
    REG(m, TFW_ETH_DMACTDTPR) = desc_addr(&m->tx[m->tx_ix]);
    return 0;
}

int tfw_eth_mac_rx(tfw_eth_mac_t *m, void *out, size_t cap)
{
    uint32_t i = m->rx_ix;
    tfw_eth_desc_t *d = &m->rx[i];
    uint32_t des3 = d->des3;
    int ret;

    if ((des3 & TFW_ETH_DES3_OWN) != 0u) {
        return 0;
    }
    eth_fence();
    {
        uint32_t len = des3 & TFW_ETH_DES3_PL;
        uint32_t whole = TFW_ETH_DES3_FD | TFW_ETH_DES3_LD;

        if ((des3 & TFW_ETH_DES3_ES) != 0u || (des3 & whole) != whole ||
            len == 0u || len > cap || len > TFW_ETH_BUF_SIZE) {
            ret = -1;
        } else {
            const uint8_t *src = m->rxbuf[i];
            uint8_t *dst = out;

            for (uint32_t k = 0u; k < len; k++) {
                dst[k] = src[k];
            }
            ret = (int)len;
        }
    }
    rx_arm(m, i);
    eth_fence();
    REG(m, TFW_ETH_DMACRDTPR) = desc_addr(d);
    m->rx_ix = (i + 1u) % TFW_ETH_DESC_COUNT;
    return ret;
}
