/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * I2C, SPI and 1-Wire -- and the carve-out that keeps the secure element from
 * being reachable as an ordinary peripheral.
 */

#include <string.h>
#include "unity.h"
#include "bus.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static void test_an_i2c_transfer_reaches_the_addressed_device(void)
{
    const uint8_t resp[3] = { 0xAA, 0xBB, 0xCC };
    tfw_mock_set_bus_response(resp, 3u);

    /* addr 0x48, read 3, write one register byte */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x48u, 3u, 0x00u }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0x48u, tfw_mock_last_i2c_addr());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, outbuf, 3u);
}

/*
 * THE CARVE-OUT.
 *
 * The ATECC608A sits on the same I2C bus. A generic transfer that could address
 * it would let a host talk to the element around the firmware's crypto layer --
 * reading its configuration, exhausting its monotonic counters, or issuing
 * commands the attestation path assumes only it issues. The device's own root
 * of trust would become an ordinary peripheral.
 */
static void test_the_secure_elements_address_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x60u, 1u, 0x00u }, 3u));
    /* and the transfer never happened */
    TEST_ASSERT_NOT_EQUAL(0x60u, tfw_mock_last_i2c_addr());
}

/* Refused whether or not an element is fitted. A build that allowed it when
 * absent would allow it the moment one was added. */
static void test_the_reserved_address_is_refused_even_with_no_element_present(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x60u, 0u, 0x01u }, 3u));
}

static void test_an_address_beyond_seven_bits_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0xFFu, 1u, 0x00u }, 3u));
}

static void test_a_read_longer_than_a_reply_can_carry_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x48u, 200u, 0x00u }, 3u));
}

static void test_an_spi_transfer_is_full_duplex(void)
{
    const uint8_t resp[2] = { 0x12, 0x34 };
    tfw_mock_set_bus_response(resp, 2u);

    /* cs on pin 10, two bytes out */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SPI_XFER, (const uint8_t[]){ 10u, 0xAAu, 0xBBu }, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, outbuf, 2u);
}

/* A reserved chip-select -- a TPM's, or the UART's pins -- is refused for the
 * same reason the element's address is. */
static void test_a_reserved_chip_select_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
        run(TFW_CMD_SPI_XFER, (const uint8_t[]){ 1u, 0xAAu }, 2u));
}

static void test_a_onewire_reset_reports_presence(void)
{
    tfw_mock_ow_presence_set(1u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);

    tfw_mock_ow_presence_set(0u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
}

/* A DS18B20 exchange, as the host would drive it: reset, write a command,
 * read the scratchpad. No part of that sequence is in the firmware. */
static void test_a_onewire_sensor_exchange_needs_no_sensor_code_in_firmware(void)
{
    const uint8_t scratchpad[2] = { 0x50, 0x05 };   /* +85.0 C, the power-on value */
    tfw_mock_ow_presence_set(1u);
    tfw_mock_set_bus_response(scratchpad, 2u);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_WRITE, (const uint8_t[]){ 9u, 0xCCu, 0xBEu }, 3u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_ONEWIRE_READ,  (const uint8_t[]){ 9u, 2u }, 2u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(scratchpad, outbuf, 2u);
}

static void test_onewire_on_a_reserved_pin_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run(TFW_CMD_ONEWIRE_RESET, (const uint8_t[]){ 1u }, 1u));
}

static void test_a_zero_length_bus_transfer_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_ONEWIRE_READ, (const uint8_t[]){ 9u, 0u }, 2u));
}

/* I2C_CONFIG: a sensible clock reaches tfw_hal_i2c_init(), and one outside the
 * interval the wire accepts is refused before it does. */
static void test_i2c_config_applies_a_clock_within_range(void)
{
    /* 400 kHz, little-endian: 0x00061A80 */
    TEST_ASSERT_EQUAL(TFW_OK,
        run(TFW_CMD_I2C_CONFIG, (const uint8_t[]){ 0x80u, 0x1Au, 0x06u, 0x00u }, 4u));
    TEST_ASSERT_EQUAL_UINT32(400000uL, tfw_mock_last_i2c_hz());
}

static void test_i2c_config_refuses_a_clock_below_the_floor(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_I2C_CONFIG, (const uint8_t[]){ 0u, 0u, 0u, 0u }, 4u));
    /* and the HAL never saw it -- the mock's reset value is out of range for
     * any real call, so this is not confused with a genuine zero request. */
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFuL, tfw_mock_last_i2c_hz());
}

static void test_i2c_config_refuses_a_clock_above_the_ceiling(void)
{
    /* 2 MHz: 0x001E8480, past TFW_I2C_HZ_MAX (400 kHz) */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_I2C_CONFIG, (const uint8_t[]){ 0x80u, 0x84u, 0x1Eu, 0x00u }, 4u));
}

static void test_i2c_config_rejects_the_wrong_argument_count(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC,
        run(TFW_CMD_I2C_CONFIG, (const uint8_t[]){ 0x80u, 0x1Au, 0x06u }, 3u));
}

/* SPI_CONFIG: mode, clkdiv and bit order all reach tfw_hal_spi_init() intact,
 * and each of the three is bounded independently. */
static void test_spi_config_applies_mode_clkdiv_and_bit_order(void)
{
    TEST_ASSERT_EQUAL(TFW_OK,
        run(TFW_CMD_SPI_CONFIG, (const uint8_t[]){ 3u, 2u, 1u }, 3u));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_mock_last_spi_clkdiv());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_mock_last_spi_lsb());
}

static void test_spi_config_refuses_a_mode_past_three(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_SPI_CONFIG, (const uint8_t[]){ 4u, 0u, 0u }, 3u));
}

static void test_spi_config_refuses_a_clkdiv_past_three(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_SPI_CONFIG, (const uint8_t[]){ 0u, 4u, 0u }, 3u));
}

static void test_spi_config_refuses_a_bit_order_past_one(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
        run(TFW_CMD_SPI_CONFIG, (const uint8_t[]){ 0u, 0u, 2u }, 3u));
}

static void test_spi_config_rejects_the_wrong_argument_count(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC,
        run(TFW_CMD_SPI_CONFIG, (const uint8_t[]){ 0u, 0u }, 2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_i2c_transfer_reaches_the_addressed_device);
    RUN_TEST(test_the_secure_elements_address_is_refused);
    RUN_TEST(test_the_reserved_address_is_refused_even_with_no_element_present);
    RUN_TEST(test_an_address_beyond_seven_bits_is_refused);
    RUN_TEST(test_a_read_longer_than_a_reply_can_carry_is_refused);
    RUN_TEST(test_an_spi_transfer_is_full_duplex);
    RUN_TEST(test_a_reserved_chip_select_is_refused);
    RUN_TEST(test_a_onewire_reset_reports_presence);
    RUN_TEST(test_a_onewire_sensor_exchange_needs_no_sensor_code_in_firmware);
    RUN_TEST(test_onewire_on_a_reserved_pin_is_refused);
    RUN_TEST(test_a_zero_length_bus_transfer_is_refused);
    RUN_TEST(test_i2c_config_applies_a_clock_within_range);
    RUN_TEST(test_i2c_config_refuses_a_clock_below_the_floor);
    RUN_TEST(test_i2c_config_refuses_a_clock_above_the_ceiling);
    RUN_TEST(test_i2c_config_rejects_the_wrong_argument_count);
    RUN_TEST(test_spi_config_applies_mode_clkdiv_and_bit_order);
    RUN_TEST(test_spi_config_refuses_a_mode_past_three);
    RUN_TEST(test_spi_config_refuses_a_clkdiv_past_three);
    RUN_TEST(test_spi_config_refuses_a_bit_order_past_one);
    RUN_TEST(test_spi_config_rejects_the_wrong_argument_count);
    return UNITY_END();
}
