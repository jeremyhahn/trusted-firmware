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
#include "ag/protocol.h"
#include "ag/config.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

void setUp(void)    { ag_mock_reset(); }
void tearDown(void) {}

static ag_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    ag_reader_init(&r, inbuf, n);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_dispatch(cmd, &r, &w);
}

static void test_an_i2c_transfer_reaches_the_addressed_device(void)
{
    const uint8_t resp[3] = { 0xAA, 0xBB, 0xCC };
    ag_mock_set_bus_response(resp, 3u);

    /* addr 0x48, read 3, write one register byte */
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_I2C_XFER, (const uint8_t[]){ 0x48u, 3u, 0x00u }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0x48u, ag_mock_last_i2c_addr());
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
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION,
        run(AG_CMD_I2C_XFER, (const uint8_t[]){ 0x60u, 1u, 0x00u }, 3u));
    /* and the transfer never happened */
    TEST_ASSERT_NOT_EQUAL(0x60u, ag_mock_last_i2c_addr());
}

/* Refused whether or not an element is fitted. A build that allowed it when
 * absent would allow it the moment one was added. */
static void test_the_reserved_address_is_refused_even_with_no_element_present(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION,
        run(AG_CMD_I2C_XFER, (const uint8_t[]){ 0x60u, 0u, 0x01u }, 3u));
}

static void test_an_address_beyond_seven_bits_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
        run(AG_CMD_I2C_XFER, (const uint8_t[]){ 0xFFu, 1u, 0x00u }, 3u));
}

static void test_a_read_longer_than_a_reply_can_carry_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
        run(AG_CMD_I2C_XFER, (const uint8_t[]){ 0x48u, 200u, 0x00u }, 3u));
}

static void test_an_spi_transfer_is_full_duplex(void)
{
    const uint8_t resp[2] = { 0x12, 0x34 };
    ag_mock_set_bus_response(resp, 2u);

    /* cs on pin 10, two bytes out */
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SPI_XFER, (const uint8_t[]){ 10u, 0xAAu, 0xBBu }, 3u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, outbuf, 2u);
}

/* A reserved chip-select -- a TPM's, or the UART's pins -- is refused for the
 * same reason the element's address is. */
static void test_a_reserved_chip_select_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION,
        run(AG_CMD_SPI_XFER, (const uint8_t[]){ 1u, 0xAAu }, 2u));
}

static void test_a_onewire_reset_reports_presence(void)
{
    ag_mock_ow_presence_set(1u);
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, outbuf[0]);

    ag_mock_ow_presence_set(0u);
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, outbuf[0]);
}

/* A DS18B20 exchange, as the host would drive it: reset, write a command,
 * read the scratchpad. No part of that sequence is in the firmware. */
static void test_a_onewire_sensor_exchange_needs_no_sensor_code_in_firmware(void)
{
    const uint8_t scratchpad[2] = { 0x50, 0x05 };   /* +85.0 C, the power-on value */
    ag_mock_ow_presence_set(1u);
    ag_mock_set_bus_response(scratchpad, 2u);

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_ONEWIRE_RESET, (const uint8_t[]){ 9u }, 1u));
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_ONEWIRE_WRITE, (const uint8_t[]){ 9u, 0xCCu, 0xBEu }, 3u));
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_ONEWIRE_READ,  (const uint8_t[]){ 9u, 2u }, 2u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(scratchpad, outbuf, 2u);
}

static void test_onewire_on_a_reserved_pin_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RESERVED_PIN, run(AG_CMD_ONEWIRE_RESET, (const uint8_t[]){ 1u }, 1u));
}

static void test_a_zero_length_bus_transfer_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(AG_CMD_ONEWIRE_READ, (const uint8_t[]){ 9u, 0u }, 2u));
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
    return UNITY_END();
}
