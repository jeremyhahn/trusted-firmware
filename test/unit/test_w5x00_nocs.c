/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The W5x00 driver with its chip select named on a pin that has no digital
 * port: TFW_ETH_CS_PIN is 21 (A7 on the host atmega328p, ADC only) on the
 * command line. The board lookup refuses the pin, so the bus is never brought
 * up and start reports the part unsupported without a single register write.
 * Built twice, as test_w5x00.c is: W5X_TEST_CHIP is 1 for test_w5100_nocs
 * (w5100.c linked) and 2 for test_w5500_nocs (w5500.c linked).
 */

#include "unity.h"
#include "tfw/eth.h"
#include "tfw/board.h"
#include "w5x00_model.h"

#define CHIP   ((uint8_t)W5X_TEST_CHIP)

static const tfw_netcfg_t cfg = {
    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u },
    { 192u, 0u, 2u, 10u },
    { 255u, 255u, 255u, 0u },
    { 192u, 0u, 2u, 1u },
    { 192u, 0u, 2u, 20u },
    7020u,
    0u                                         /* static addresses */
};

void setUp(void)    { w5x_model_reset(CHIP); }
void tearDown(void) { }

static void test_the_chip_select_pin_has_no_digital_port(void)
{
    tfw_pinref_t cs;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pin(TFW_ETH_CS_PIN, &cs));
}

static void test_start_refuses_a_chip_select_with_no_port_and_never_writes(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_eth_start(&cfg, 50000u));
    TEST_ASSERT_EQUAL_UINT32(0u, w5x_model_writes());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_chip_select_pin_has_no_digital_port);
    RUN_TEST(test_start_refuses_a_chip_select_with_no_port_and_never_writes);
    return UNITY_END();
}
