/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * EEPROM, and the protected region that keeps the generic primitive from
 * defeating the root of trust.
 */

#include <string.h>
#include "unity.h"
#include "storage.h"
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

/* Sized for the largest request the protocol can carry, not for the largest
 * one the device will accept -- the over-chunk case deliberately sends more
 * than AG_EEPROM_CHUNK, and a buffer sized to the accepted maximum would
 * overflow here while testing that the device refuses it. */
static ag_status_t ee_write(uint16_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t p[2 + AG_MAX_PAYLOAD];

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(AG_MAX_PAYLOAD, n);
    p[0] = (uint8_t)(addr & 0xFFu);
    p[1] = (uint8_t)(addr >> 8);
    memcpy(&p[2], data, n);
    return run(AG_CMD_EEPROM_WRITE, p, (uint8_t)(2u + n));
}

static ag_status_t ee_read(uint16_t addr, uint8_t n)
{
    uint8_t p[3] = { (uint8_t)(addr & 0xFFu), (uint8_t)(addr >> 8), n };
    return run(AG_CMD_EEPROM_READ, p, 3u);
}

static void test_a_round_trip_through_the_user_area(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    TEST_ASSERT_EQUAL(AG_OK, ee_write(0x100u, data, 4u));
    TEST_ASSERT_EQUAL(AG_OK, ee_read(0x100u, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, outbuf, 4u);
}

/*
 * THE SECURITY BOUNDARY.
 *
 * The low 128 bytes hold the bootloader's public-key hash. Without this check,
 * the generic EEPROM primitive would let anyone on the serial line overwrite
 * the hash the bootloader verifies against -- the whole root of trust, defeated
 * by a documented command doing exactly what it says it does.
 */
static void test_the_reserved_region_cannot_be_written(void)
{
    const uint8_t evil[4] = { 0, 0, 0, 0 };

    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ee_write(0x000u, evil, 4u)); /* pubkey hash */
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ee_write(0x020u, evil, 4u)); /* device serial */
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ee_write(0x030u, evil, 4u)); /* boot counter */
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ee_write(0x07Fu, evil, 1u)); /* last byte of it */
}

/* A write beginning below the boundary and running past it must be refused
 * whole. Checking only the starting address would let a write at 0x7F running
 * eight bytes reach 0x86 -- through the boundary, having passed the check. */
static void test_a_write_straddling_the_boundary_is_refused_entirely(void)
{
    const uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ee_write(0x07Fu, data, 8u));
    /* and nothing was written */
    TEST_ASSERT_EQUAL_HEX8(0xFFu, ag_mock_eeprom()[0x07F]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, ag_mock_eeprom()[0x080]);
}

static void test_the_first_byte_above_the_boundary_is_writable(void)
{
    const uint8_t one[1] = { 0x5A };
    TEST_ASSERT_EQUAL(AG_OK, ee_write(0x080u, one, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, ag_mock_eeprom()[0x080]);
}

/* Reading the reserved region IS allowed. Its contents are a hash and a serial
 * number -- public by nature -- and refusing them would break provisioning
 * tools for no security gain. */
static void test_the_reserved_region_can_be_read(void)
{
    ag_mock_eeprom()[0x000] = 0xAB;
    TEST_ASSERT_EQUAL(AG_OK, ee_read(0x000u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xABu, outbuf[0]);
}

static void test_an_address_past_the_part_is_refused(void)
{
    const uint8_t one[1] = { 0 };
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ee_write(2000u, one, 1u));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ee_read(2000u, 1u));
}

/* A length that would run off the end must be refused, and the check is
 * written to avoid overflowing addr + len -- reachable from a malformed
 * request rather than theoretical on a uint16_t address. */
static void test_a_length_running_past_the_end_is_refused(void)
{
    const uint8_t data[8] = { 0 };
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ee_write(1020u, data, 8u));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ee_read(1020u, 8u));
}

/* Chunked so a bulk write cannot hold the main loop. A byte takes ~3.4 ms, so
 * 64 bytes would block for 220 ms -- long enough to miss a scheduled deadline. */
static void test_a_write_larger_than_the_chunk_is_refused(void)
{
    const uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ee_write(0x100u, data, 16u));
}

static void test_a_zero_length_write_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(AG_CMD_EEPROM_WRITE, (const uint8_t[]){ 0u, 1u }, 2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_round_trip_through_the_user_area);
    RUN_TEST(test_the_reserved_region_cannot_be_written);
    RUN_TEST(test_a_write_straddling_the_boundary_is_refused_entirely);
    RUN_TEST(test_the_first_byte_above_the_boundary_is_writable);
    RUN_TEST(test_the_reserved_region_can_be_read);
    RUN_TEST(test_an_address_past_the_part_is_refused);
    RUN_TEST(test_a_length_running_past_the_end_is_refused);
    RUN_TEST(test_a_write_larger_than_the_chunk_is_refused);
    RUN_TEST(test_a_zero_length_write_is_refused);
    return UNITY_END();
}
