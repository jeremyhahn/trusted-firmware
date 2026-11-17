/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H573I-DK's Ethernet PHY over MDIO (#579, slice 1 of the Ethernet
 * ADR, docs/multi-arch/adr-stm32h5-ethernet.md): the MAC's clocks, the RMII
 * select and pins, an MDIO read and write, a scan of addresses 0-31 and the
 * PHY's identifier and link. No MAC, DMA or IP stack: that is slice 2.
 *
 * The decode below is the IEEE 802.3 clause 22 register layout, pure, and
 * host-tested by test/unit/test_h5_eth_mdio.c; the register driver is
 * hal_eth_mdio.c.
 */
#ifndef TFW_H5_ETH_MDIO_H
#define TFW_H5_ETH_MDIO_H

#include <stddef.h>
#include <stdint.h>

/* The board profile turns the boot probe on (ETH_PROBE=1); off elsewhere. */
#ifndef TFW_H5_ETH_PROBE
#define TFW_H5_ETH_PROBE 0
#endif

/* Clause 22 registers. */
#define TFW_PHY_REG_BMSR     1u
#define TFW_PHY_REG_PHYID1   2u
#define TFW_PHY_REG_PHYID2   3u
#define TFW_PHY_BMSR_LINK    (1u << 2)     /* link status, latched low */
#define TFW_PHY_ADDR_COUNT   32u

/* One MDIO read: 0 and *val on success, nonzero when the MAC never finished. */
typedef int (*tfw_mdio_read_fn)(uint8_t addr, uint8_t reg, uint16_t *val);

typedef struct {
    uint8_t  addr;       /* the MDIO address that answered */
    uint16_t id1;        /* PHYID1, register 2 */
    uint16_t id2;        /* PHYID2, register 3 */
    uint8_t  link_up;    /* BMSR bit 2 on the second read */
} tfw_phy_probe_t;

/* OUI bits 3-18 are PHYID1, bits 19-24 are PHYID2[15:10]: 22 bits. */
static inline uint32_t tfw_phy_oui(uint16_t id1, uint16_t id2)
{
    return ((uint32_t)id1 << 6) | ((uint32_t)id2 >> 10);
}

static inline uint8_t tfw_phy_model(uint16_t id2)
{
    return (uint8_t)((id2 >> 4) & 0x3Fu);
}

static inline uint8_t tfw_phy_revision(uint16_t id2)
{
    return (uint8_t)(id2 & 0x0Fu);
}

/* No PHY drives the line: an idle MDIO reads all ones (pulled up) or zeros. */
static inline int tfw_phy_id_present(uint16_t id1, uint16_t id2)
{
    return !((id1 == 0xFFFFu && id2 == 0xFFFFu) || (id1 == 0x0000u && id2 == 0x0000u));
}

/* BMSR's link bit latches low until read, so the second read is the state. */
static inline int tfw_phy_link_up(uint16_t bmsr_second)
{
    return (bmsr_second & TFW_PHY_BMSR_LINK) != 0u;
}

/* The first address 0-31 whose identifier is present: 0 and *out, or -1. */
static inline int tfw_phy_scan(tfw_mdio_read_fn rd, tfw_phy_probe_t *out)
{
    for (uint8_t a = 0u; a < TFW_PHY_ADDR_COUNT; a++) {
        uint16_t id1 = 0u, id2 = 0u, bmsr = 0u;

        if (rd(a, TFW_PHY_REG_PHYID1, &id1) != 0 || rd(a, TFW_PHY_REG_PHYID2, &id2) != 0) {
            continue;
        }
        if (!tfw_phy_id_present(id1, id2)) {
            continue;
        }
        out->addr = a;
        out->id1 = id1;
        out->id2 = id2;
        out->link_up = 0u;
        if (rd(a, TFW_PHY_REG_BMSR, &bmsr) == 0 && rd(a, TFW_PHY_REG_BMSR, &bmsr) == 0) {
            out->link_up = (uint8_t)tfw_phy_link_up(bmsr);
        }
        return 0;
    }
    return -1;
}

static inline char tfw_phy_hex(uint32_t v)
{
    v &= 0xFu;
    return (char)(v < 10u ? '0' + v : 'A' + (v - 10u));
}

/*
 * The boot line: "eth: phy addr=N id=XXXX:XXXX link=up|down\r\n", or
 * "eth: phy none\r\n" when p is NULL. Returns the length written (no NUL
 * counted) or 0 if buf is too small (48 bytes always fit).
 */
static inline size_t tfw_phy_format(const tfw_phy_probe_t *p, char *buf, size_t cap)
{
    size_t n = 0u;
    const char *s;
    char tmp[48];

    if (p == NULL) {
        s = "eth: phy none\r\n";
        while (*s != '\0') {
            tmp[n++] = *s++;
        }
    } else {
        s = "eth: phy addr=";
        while (*s != '\0') {
            tmp[n++] = *s++;
        }
        if (p->addr >= 10u) {
            tmp[n++] = (char)('0' + p->addr / 10u);
        }
        tmp[n++] = (char)('0' + p->addr % 10u);
        s = " id=";
        while (*s != '\0') {
            tmp[n++] = *s++;
        }
        for (int sh = 12; sh >= 0; sh -= 4) {
            tmp[n++] = tfw_phy_hex((uint32_t)p->id1 >> sh);
        }
        tmp[n++] = ':';
        for (int sh = 12; sh >= 0; sh -= 4) {
            tmp[n++] = tfw_phy_hex((uint32_t)p->id2 >> sh);
        }
        s = p->link_up ? " link=up\r\n" : " link=down\r\n";
        while (*s != '\0') {
            tmp[n++] = *s++;
        }
    }
    if (n + 1u > cap) {
        return 0u;
    }
    for (size_t i = 0u; i < n; i++) {
        buf[i] = tmp[i];
    }
    buf[n] = '\0';
    return n;
}

/* The register driver, hal_eth_mdio.c. */
void tfw_h5_eth_mdio_init(void);
int  tfw_h5_eth_mdio_read(uint8_t addr, uint8_t reg, uint16_t *val);
int  tfw_h5_eth_mdio_write(uint8_t addr, uint8_t reg, uint16_t val);
/* Init, scan and format the boot line into buf; returns its length. */
size_t tfw_h5_eth_probe(char *buf, size_t cap);

#endif /* TFW_H5_ETH_MDIO_H */
