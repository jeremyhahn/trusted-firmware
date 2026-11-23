/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 MDIO driver's register values (src/hal/cortex-m/stm32h5/
 * hal_eth_mdio.c, #579) on the register file (test/model/cortex-m/
 * regfile_model.c): the MACMDIOAR command a read or a write issues, the wait
 * on MACMDIOAR.MB, the spin limit, and init's SBS_PMCR, RCC and pin values.
 * The register file is built with its tfw_cm_model_reg renamed
 * regfile_model_reg (mk/host.mk), so the one in test/model/cortex-m/
 * h5_mdio_model.h can stand in for the MAC:
 * it clears MB once the driver has polled it busy_polls times. The
 * addresses are the driver's, not confirmed against RM0481. A board has not
 * run them.
 */
#include <stdint.h>

#include "unity.h"
#include "soc_regs.h"
#include "regfile_model.h"
#include "h5_mdio_model.h"
#include "hal_eth_mdio.h"

#define AHB1ENR    (TFW_H5_RCC_BASE + 0x88u)
#define SBS_PMCR   (0x44000400u + 0x100u)
#define GOC_WRITE  (0x1u << 2)

void setUp(void)
{
    regfile_model_reset();
    busy_polls = 3u;
    polls = 0u;
    issued = 0u;
    issued_dr = 0u;
    seen = 0u;
    reply = 0u;
}

void tearDown(void)
{
}

void test_a_read_names_the_phy_and_register_and_waits_on_mb(void)
{
    uint16_t v = 0u;

    regfile_model_poke(MACMDIODR, 0x5A5A5A5Au);   /* stale until MB clears */
    reply = 0xABCD1234u;
    TEST_ASSERT_EQUAL_INT(0, tfw_h5_eth_mdio_read(0x01u, 0x02u, &v));
    TEST_ASSERT_EQUAL_HEX32((0x01u << 21) | (0x02u << 16) | GOC_READ | MB, issued);
    TEST_ASSERT_EQUAL_UINT32(3u, seen);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, v);
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(MACMDIOAR) & MB);
}

void test_a_write_loads_dr_then_issues_the_write_and_waits_on_mb(void)
{
    regfile_model_poke(MACMDIODR, 0x5A5A5A5Au); /* stale */
    TEST_ASSERT_EQUAL_INT(0, tfw_h5_eth_mdio_write(0x1Fu, 0x00u, 0x8000u));
    TEST_ASSERT_EQUAL_HEX32((0x1Fu << 21) | GOC_WRITE | MB, issued);
    TEST_ASSERT_EQUAL_HEX32(0x8000u, issued_dr);
    TEST_ASSERT_EQUAL_HEX32(0x8000u, regfile_model_peek(MACMDIODR));
    TEST_ASSERT_EQUAL_UINT32(3u, seen);
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(MACMDIOAR) & MB);
}

void test_a_phy_or_register_past_five_bits_is_masked(void)
{
    uint16_t v = 0u;

    TEST_ASSERT_EQUAL_INT(0, tfw_h5_eth_mdio_read(0x21u, 0x3Fu, &v));
    TEST_ASSERT_EQUAL_HEX32((0x01u << 21) | (0x1Fu << 16) | GOC_READ | MB, issued);
}

void test_mb_held_past_the_spin_limit_fails_a_read(void)
{
    uint16_t v = 0xA5A5u;

    busy_polls = NEVER;
    regfile_model_poke(MACMDIOAR, MB);             /* a command that never ends */
    regfile_model_poke(MACMDIODR, 0x1234u);
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_eth_mdio_read(0x01u, 0x02u, &v));
    TEST_ASSERT_EQUAL_HEX16(0xA5A5u, v);
    TEST_ASSERT_EQUAL_HEX32(MB, regfile_model_peek(MACMDIOAR));   /* nothing issued */
    TEST_ASSERT_EQUAL_UINT32(100000u, seen);   /* MDIO_SPIN polls */
}

void test_mb_held_past_the_spin_limit_fails_a_write(void)
{
    busy_polls = NEVER;
    regfile_model_poke(MACMDIOAR, MB);
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_eth_mdio_write(0x01u, 0x00u, 0x8000u));
    TEST_ASSERT_EQUAL_HEX32(MB, regfile_model_peek(MACMDIOAR));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(MACMDIODR));
}

void test_a_command_that_never_ends_fails_the_read(void)
{
    uint16_t v = 0xA5A5u;

    busy_polls = NEVER;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_eth_mdio_read(0x01u, 0x02u, &v));
    TEST_ASSERT_EQUAL_HEX32((0x01u << 21) | (0x02u << 16) | GOC_READ | MB, issued);
    TEST_ASSERT_EQUAL_HEX16(0xA5A5u, v);
}

void test_a_command_that_never_ends_fails_the_write(void)
{
    busy_polls = NEVER;
    TEST_ASSERT_EQUAL_INT(-1, tfw_h5_eth_mdio_write(0x01u, 0x00u, 0x8000u));
    TEST_ASSERT_EQUAL_HEX32((0x01u << 21) | GOC_WRITE | MB, issued);
}

void test_init_selects_rmii_and_keeps_the_rest_of_pmcr(void)
{
    regfile_model_poke(SBS_PMCR, 0x00E00001u);   /* ETH_SEL_PHY 7, one other bit */
    tfw_h5_eth_mdio_init();
    TEST_ASSERT_EQUAL_HEX32((0x4u << 21) | 0x1u, regfile_model_peek(SBS_PMCR));
    TEST_ASSERT_EQUAL_HEX32(0x1u << 1, regfile_model_peek(TFW_H5_RCC_BASE + 0xA8u) & (0x1u << 1));
}

void test_init_enables_the_mac_clocks_and_keeps_the_others(void)
{
    regfile_model_poke(AHB1ENR, 0x1u);
    tfw_h5_eth_mdio_init();
    TEST_ASSERT_EQUAL_HEX32(0x1u | (0x1u << 19) | (0x1u << 20) | (0x1u << 21),
                            regfile_model_peek(AHB1ENR));
}

static const uint8_t rmii_pins[][2] = {
    {0u, 1u}, {0u, 2u}, {0u, 7u},       /* PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV */
    {2u, 1u}, {2u, 4u}, {2u, 5u},       /* PC1 MDC, PC4 RXD0, PC5 RXD1 */
    {6u, 11u}, {6u, 12u}, {6u, 13u},    /* PG11 TX_EN, PG12 TXD1, PG13 TXD0 */
};

void test_init_puts_the_rmii_pins_on_af11(void)
{
    size_t i;

    regfile_model_poke(TFW_H5_GPIO_STRIDE * 0u + TFW_H5_GPIOA_BASE, 0xFFFFFFFFu);
    regfile_model_poke(TFW_H5_GPIO_STRIDE * 2u + TFW_H5_GPIOA_BASE, 0xFFFFFFFFu);
    regfile_model_poke(TFW_H5_GPIO_STRIDE * 6u + TFW_H5_GPIOA_BASE, 0xFFFFFFFFu);
    tfw_h5_eth_mdio_init();
    for (i = 0u; i < sizeof rmii_pins / sizeof rmii_pins[0]; i++) {
        uint32_t b = TFW_H5_GPIOA_BASE + TFW_H5_GPIO_STRIDE * rmii_pins[i][0];
        uint8_t pin = rmii_pins[i][1];
        uint32_t afr = regfile_model_peek(b + 0x20u + 4u * (pin >> 3));

        TEST_ASSERT_EQUAL_HEX32(0x2u, (regfile_model_peek(b + 0x00u) >> (2u * pin)) & 0x3u);
        TEST_ASSERT_EQUAL_HEX32(11u, (afr >> (4u * (pin & 7u))) & 0xFu);
        TEST_ASSERT_EQUAL_HEX32(0x3u, (regfile_model_peek(b + 0x08u) >> (2u * pin)) & 0x3u);
        TEST_ASSERT_EQUAL_HEX32(0x0u, (regfile_model_peek(b + 0x0Cu) >> (2u * pin)) & 0x3u);
    }
    /* PA0, a pin init does not name, stays analog */
    TEST_ASSERT_EQUAL_HEX32(0x3u, regfile_model_peek(TFW_H5_GPIOA_BASE) & 0x3u);
    TEST_ASSERT_EQUAL_HEX32((0x1u << 0) | (0x1u << 2) | (0x1u << 6),
                            regfile_model_peek(TFW_H5_RCC_BASE + 0x8Cu));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_read_names_the_phy_and_register_and_waits_on_mb);
    RUN_TEST(test_a_write_loads_dr_then_issues_the_write_and_waits_on_mb);
    RUN_TEST(test_a_phy_or_register_past_five_bits_is_masked);
    RUN_TEST(test_mb_held_past_the_spin_limit_fails_a_read);
    RUN_TEST(test_mb_held_past_the_spin_limit_fails_a_write);
    RUN_TEST(test_a_command_that_never_ends_fails_the_read);
    RUN_TEST(test_a_command_that_never_ends_fails_the_write);
    RUN_TEST(test_init_selects_rmii_and_keeps_the_rest_of_pmcr);
    RUN_TEST(test_init_enables_the_mac_clocks_and_keeps_the_others);
    RUN_TEST(test_init_puts_the_rmii_pins_on_af11);
    return UNITY_END();
}
