/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-bus stand-in, src/caps/bus_none.c (#163). A board with every bus off
 * links it where bus.c and the HAL's bus file would be, and main.c calls
 * tfw_bus_init() unconditionally. Its whole contract is that it touches
 * nothing: no I2C clock, no SPI mode, no pin, no EEPROM. Linked against the
 * mock HAL alone, so a stub that starts to bring a bus up -- bus.c's body
 * pasted back in, or one half of it -- shows in the mock's recorded state.
 */
#include "unity.h"
#include "hal_mock.h"
#include "bus.h"

#include <stdint.h>

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

static void assert_untouched(void)
{
    uint16_t lo = 0u, hi = 0u;

    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, tfw_mock_last_i2c_hz());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_i2c_addr());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_clkdiv());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_lsb());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_ow_read_count());
    TEST_ASSERT_EQUAL_size_t(0u, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_mock_tx_pending());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_watchdog_arms());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_irq_enabled());
}

/* bus.c's init sets the I2C clock first; the stand-in must not. */
static void test_bus_none_init_sets_no_i2c_clock(void)
{
    tfw_bus_init();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFuL, tfw_mock_last_i2c_hz());
}

/* ...and the SPI mode, divider and bit order second; nor those. */
static void test_bus_none_init_sets_no_spi_mode(void)
{
    tfw_bus_init();
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_clkdiv());
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_last_spi_lsb());
}

/* Nothing else the mock records moves either, and calling it again (a warm
 * restart runs main's init sequence once more) changes nothing. */
static void test_bus_none_init_twice_touches_nothing(void)
{
    tfw_bus_init();
    assert_untouched();
    tfw_bus_init();
    assert_untouched();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bus_none_init_sets_no_i2c_clock);
    RUN_TEST(test_bus_none_init_sets_no_spi_mode);
    RUN_TEST(test_bus_none_init_twice_touches_nothing);
    return UNITY_END();
}
