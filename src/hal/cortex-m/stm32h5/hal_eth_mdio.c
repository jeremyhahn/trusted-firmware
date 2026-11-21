/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H573I-DK's Ethernet PHY over the MAC's MDIO (#579). Sources, read
 * at the commits docs/boards/stm32h573i-dk.md pins:
 *
 *  - CMSIS header stm32h573xx.h: ETH_BASE_NS (AHB1 + 0x8000), ETH_TypeDef
 *    (MACMDIOAR, MACMDIODR), ETH_MACMDIOAR_MB/GOC/CR/RDA/PA, CR_DIV42 (0, CSR
 *    clock 60-100 MHz, so 64 MHz / 42 = 1.52 MHz MDC, under IEEE's 2.5 MHz),
 *    RCC_AHB1ENR_ETHEN/ETHTXEN/ETHRXEN (19/20/21), SBS_BASE_NS (APB3 +
 *    0x400), SBS PMCR at 0x100, SBS_PMCR_ETH_SEL_PHY (bits 23:21; RMII is
 *    SBS_PMCR_ETH_SEL_PHY_2).
 *  - Pin data, the kit's board file: PA1 ETH_REF_CLK, PA2 ETH_MDIO, PA7
 *    ETH_CRS_DV, PC1 ETH_MDC, PC4 ETH_RXD0, PC5 ETH_RXD1, PG11 ETH_TX_EN,
 *    PG12 ETH_TXD1, PG13 ETH_TXD0, ETH.MediaInterface=HAL_ETH_RMII_MODE;
 *    each on alternate function 11 (GPIO_AF11_ETH).
 *
 * Offsets and bits are **not confirmed** against RM0481, as the rest of the
 * port's are.
 */
#include "hal_eth_mdio.h"
#include "soc_regs.h"

#define ETH_BASE            0x40028000u
#define ETH_MACMDIOAR       TFW_CM_REG(ETH_BASE + 0x200u)
#define ETH_MACMDIODR       TFW_CM_REG(ETH_BASE + 0x204u)
#define MDIOAR_MB           (1u << 0)
#define MDIOAR_GOC_WRITE    (0x1u << 2)
#define MDIOAR_GOC_READ     (0x3u << 2)
#define MDIOAR_CR_DIV42     (0x0u << 8)
#define MDIOAR_RDA(r)       (((uint32_t)(r) & 0x1Fu) << 16)
#define MDIOAR_PA(a)        (((uint32_t)(a) & 0x1Fu) << 21)
#define MDIO_SPIN           100000u

#define RCC_AHB1ENR         TFW_CM_REG(TFW_H5_RCC_BASE + 0x88u)
#define RCC_AHB1ENR_ETH     ((1u << 19) | (1u << 20) | (1u << 21))
#define RCC_APB3ENR_SBSEN   (1u << 1)
#define SBS_PMCR            TFW_CM_REG(0x44000400u + 0x100u)
#define SBS_PMCR_ETH_SEL    (0x7u << 21)
#define SBS_PMCR_ETH_RMII   (0x4u << 21)

#define GPIO_OSPEEDR(b)     TFW_CM_REG((b) + 0x08u)
#define ETH_AF              11u

/* bank index (A=0), pin */
static const uint8_t eth_pins[][2] = {
    {0u, 1u}, {0u, 2u}, {0u, 7u},            /* REF_CLK, MDIO, CRS_DV */
    {2u, 1u}, {2u, 4u}, {2u, 5u},            /* MDC, RXD0, RXD1 */
    {6u, 11u}, {6u, 12u}, {6u, 13u},         /* TX_EN, TXD1, TXD0 */
};

static void eth_pin(uint8_t bank_ix, uint8_t pin)
{
    uint32_t b = TFW_H5_GPIOA_BASE + TFW_H5_GPIO_STRIDE * bank_ix;
    uint32_t two = 0x3u << (2u * pin);
    uint32_t afs = 4u * (pin & 7u);

    TFW_H5_RCC_AHB2ENR |= 1u << bank_ix;
    (void)TFW_H5_RCC_AHB2ENR;                       /* the enable lands first */
    GPIO_OSPEEDR(b) |= two;                         /* very high: 50 MHz REF_CLK */
    TFW_H5_GPIO_PUPDR(b) &= ~two;
    TFW_H5_GPIO_AFR(b, pin >> 3) = (TFW_H5_GPIO_AFR(b, pin >> 3) & ~(0xFu << afs)) |
                                   (ETH_AF << afs);
    TFW_H5_GPIO_MODER(b) = (TFW_H5_GPIO_MODER(b) & ~two) | (0x2u << (2u * pin));
}

void tfw_h5_eth_mdio_init(void)
{
    /* RMII is selected while the MAC's clocks are still off. */
    TFW_H5_RCC_APB3ENR |= RCC_APB3ENR_SBSEN;
    (void)TFW_H5_RCC_APB3ENR;
    SBS_PMCR = (SBS_PMCR & ~SBS_PMCR_ETH_SEL) | SBS_PMCR_ETH_RMII;

    for (size_t i = 0u; i < sizeof eth_pins / sizeof eth_pins[0]; i++) {
        eth_pin(eth_pins[i][0], eth_pins[i][1]);
    }
    RCC_AHB1ENR |= RCC_AHB1ENR_ETH;
    (void)RCC_AHB1ENR;
}

static int mdio_wait(void)
{
    for (uint32_t i = 0u; i < MDIO_SPIN; i++) {
        if ((ETH_MACMDIOAR & MDIOAR_MB) == 0u) {
            return 0;
        }
    }
    return -1;
}

int tfw_h5_eth_mdio_read(uint8_t addr, uint8_t reg, uint16_t *val)
{
    if (mdio_wait() != 0) {
        return -1;
    }
    ETH_MACMDIOAR = MDIOAR_PA(addr) | MDIOAR_RDA(reg) | MDIOAR_CR_DIV42 |
                    MDIOAR_GOC_READ | MDIOAR_MB;
    if (mdio_wait() != 0) {
        return -1;
    }
    *val = (uint16_t)(ETH_MACMDIODR & 0xFFFFu);
    return 0;
}

int tfw_h5_eth_mdio_write(uint8_t addr, uint8_t reg, uint16_t val)
{
    if (mdio_wait() != 0) {
        return -1;
    }
    ETH_MACMDIODR = val;
    ETH_MACMDIOAR = MDIOAR_PA(addr) | MDIOAR_RDA(reg) | MDIOAR_CR_DIV42 |
                    MDIOAR_GOC_WRITE | MDIOAR_MB;
    return mdio_wait();
}

size_t tfw_h5_eth_probe(char *buf, size_t cap)
{
    tfw_phy_probe_t p;

    tfw_h5_eth_mdio_init();
    if (tfw_phy_scan(tfw_h5_eth_mdio_read, &p) != 0) {
        return tfw_phy_format(NULL, buf, cap);
    }
    return tfw_phy_format(&p, buf, cap);
}
