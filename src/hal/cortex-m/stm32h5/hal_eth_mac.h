/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 Ethernet MAC and DMA (#581, slice 2 of the Ethernet ADR,
 * docs/multi-arch/adr-stm32h5-ethernet.md): MAC configuration for RMII at
 * the PHY's resolved speed and duplex, one DMA channel with four RX and four
 * TX descriptors of 1536-byte buffers, start and stop, and poll-mode
 * transmit and receive (no interrupt: lwIP runs NO_SYS from the main loop).
 *
 * Register offsets are RM0481's ETH section (the Synopsys GMAC as on the H7):
 * MACCR 0x000, MACPFR 0x008, MACA0HR/LR 0x300/0x304, MTLTQOMR 0xD00,
 * MTLRQOMR 0xD30, DMAMR 0x1000, DMASBMR 0x1004, DMACCR 0x1100, DMACTCR
 * 0x1104, DMACRCR 0x1108, DMACTDLAR 0x1114, DMACRDLAR 0x111C, DMACTDTPR
 * 0x1120, DMACRDTPR 0x1128, DMACTDRLR 0x112C, DMACRDRLR 0x1130, DMACSR
 * 0x1160.
 *
 * The driver takes the register block and the rings as pointers so that
 * test/unit/test_h5_eth_mac.c drives it over a fake block on the host.
 */
#ifndef TFW_H5_ETH_MAC_H
#define TFW_H5_ETH_MAC_H

#include <stddef.h>
#include <stdint.h>

#define TFW_ETH_DESC_COUNT   4u
#define TFW_ETH_BUF_SIZE     1536u
#define TFW_ETH_REG_WORDS    (0x1200u / 4u)   /* the block the driver touches */

/* Register byte offsets. */
#define TFW_ETH_MACCR        0x0000u
#define TFW_ETH_MACPFR       0x0008u
#define TFW_ETH_MACA0HR      0x0300u
#define TFW_ETH_MACA0LR      0x0304u
#define TFW_ETH_MTLTQOMR     0x0D00u
#define TFW_ETH_MTLRQOMR     0x0D30u
#define TFW_ETH_DMAMR        0x1000u
#define TFW_ETH_DMASBMR      0x1004u
#define TFW_ETH_DMACCR       0x1100u
#define TFW_ETH_DMACTCR      0x1104u
#define TFW_ETH_DMACRCR      0x1108u
#define TFW_ETH_DMACTDLAR    0x1114u
#define TFW_ETH_DMACRDLAR    0x111Cu
#define TFW_ETH_DMACTDTPR    0x1120u
#define TFW_ETH_DMACRDTPR    0x1128u
#define TFW_ETH_DMACTDRLR    0x112Cu
#define TFW_ETH_DMACRDRLR    0x1130u
#define TFW_ETH_DMACSR       0x1160u

/* MACCR bits. */
#define TFW_ETH_MACCR_RE     (1u << 0)
#define TFW_ETH_MACCR_TE     (1u << 1)
#define TFW_ETH_MACCR_DM     (1u << 13)     /* full duplex */
#define TFW_ETH_MACCR_FES    (1u << 14)     /* 100 Mbit/s */
#define TFW_ETH_MACCR_ACS    (1u << 20)     /* strip pad/CRC, length frames */
#define TFW_ETH_MACCR_CST    (1u << 21)     /* strip CRC, type frames */
/* Other bits. */
#define TFW_ETH_DMAMR_SWR    (1u << 0)
#define TFW_ETH_DMASBMR_FB   (1u << 0)
#define TFW_ETH_DMASBMR_AAL  (1u << 12)
#define TFW_ETH_DMA_ST_SR    (1u << 0)      /* DMACTCR.ST, DMACRCR.SR */
#define TFW_ETH_DMA_PBL32    (32u << 16)    /* TXPBL / RXPBL */
#define TFW_ETH_MTL_TSF      (1u << 1)
#define TFW_ETH_MTL_TXQEN    (2u << 2)
#define TFW_ETH_MTL_RSF      (1u << 5)

/* Descriptor DES3 bits (read and write-back formats share OWN/FD/LD). */
#define TFW_ETH_DES3_OWN     (1u << 31)
#define TFW_ETH_DES3_IOC     (1u << 30)     /* RX read format */
#define TFW_ETH_DES3_FD      (1u << 29)
#define TFW_ETH_DES3_LD      (1u << 28)
#define TFW_ETH_DES3_BUF1V   (1u << 24)     /* RX read format */
#define TFW_ETH_DES3_ES      (1u << 15)     /* RX write-back: error summary */
#define TFW_ETH_DES3_PL      0x7FFFu        /* RX write-back: packet length */

/* LAN8742A special control/status (register 31), HCDSPEED bits 4:2. */
#define TFW_PHY_REG_BMCR     0u
#define TFW_PHY_REG_PSCSR    31u
#define TFW_PHY_BMSR_ANDONE  (1u << 5)

typedef struct {
    volatile uint32_t des0;
    volatile uint32_t des1;
    volatile uint32_t des2;
    volatile uint32_t des3;
} tfw_eth_desc_t;

typedef struct {
    volatile uint32_t *regs;            /* the ETH register block */
    tfw_eth_desc_t    *tx;              /* TFW_ETH_DESC_COUNT each */
    tfw_eth_desc_t    *rx;
    uint8_t          (*txbuf)[TFW_ETH_BUF_SIZE];
    uint8_t          (*rxbuf)[TFW_ETH_BUF_SIZE];
    uint32_t           tx_ix;           /* next TX descriptor to fill */
    uint32_t           rx_ix;           /* next RX descriptor to read */
} tfw_eth_mac_t;

/* Speed and duplex from the LAN8742A's PSCSR: 1 and *maccr bits, or 0 and
 * 100M full when autonegotiation has not finished (BMSR bit 5). */
static inline int tfw_eth_phy_speed(uint16_t bmsr, uint16_t pscsr, uint32_t *maccr)
{
    uint32_t sp = ((uint32_t)pscsr >> 2) & 0x7u;

    if ((bmsr & TFW_PHY_BMSR_ANDONE) == 0u || (sp & 0x3u) == 0u || (sp & 0x3u) == 0x3u) {
        *maccr = TFW_ETH_MACCR_FES | TFW_ETH_MACCR_DM;
        return 0;
    }
    *maccr = ((sp & 0x2u) ? TFW_ETH_MACCR_FES : 0u) | ((sp & 0x4u) ? TFW_ETH_MACCR_DM : 0u);
    return 1;
}


/* Software reset of the DMA and MAC: 0, or -1 when SWR never clears. */
int  tfw_eth_mac_reset(tfw_eth_mac_t *m, uint32_t spin);
/* MAC, MTL and DMA configuration and the rings; RX descriptors handed to
 * the DMA. maccr_speed is FES/DM from tfw_eth_phy_speed. */
void tfw_eth_mac_init(tfw_eth_mac_t *m, const uint8_t mac[6], uint32_t maccr_speed);
void tfw_eth_mac_start(tfw_eth_mac_t *m);
void tfw_eth_mac_stop(tfw_eth_mac_t *m);
/* Copy one frame to the next TX descriptor and hand it to the DMA: 0, -1
 * when the DMA still owns it, -2 when len is 0 or over a buffer. */
int  tfw_eth_mac_tx(tfw_eth_mac_t *m, const void *frame, size_t len);
/* The next received frame: its length copied into out, 0 when none is
 * ready, -1 when one was dropped (error, split or larger than cap). */
int  tfw_eth_mac_rx(tfw_eth_mac_t *m, void *out, size_t cap);

#endif /* TFW_H5_ETH_MAC_H */
