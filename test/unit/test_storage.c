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

/* Sized for the largest request the protocol can carry, not for the largest
 * one the device will accept -- the over-chunk case deliberately sends more
 * than TFW_EEPROM_CHUNK, and a buffer sized to the accepted maximum would
 * overflow here while testing that the device refuses it. */
static tfw_status_t ee_write(uint16_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t p[2 + TFW_MAX_PAYLOAD];

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(TFW_MAX_PAYLOAD, n);
    p[0] = (uint8_t)(addr & 0xFFu);
    p[1] = (uint8_t)(addr >> 8);
    memcpy(&p[2], data, n);
    return run(TFW_CMD_EEPROM_WRITE, p, (uint8_t)(2u + n));
}

static tfw_status_t ee_read(uint16_t addr, uint8_t n)
{
    uint8_t p[3] = { (uint8_t)(addr & 0xFFu), (uint8_t)(addr >> 8), n };
    return run(TFW_CMD_EEPROM_READ, p, 3u);
}

static void test_a_round_trip_through_the_user_area(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x100u, data, 4u));
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x100u, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, outbuf, 4u);
}

/*
 * THE SECURITY BOUNDARY.
 *
 * The low TFW_BOOT_EE_LEN bytes (0xC0, since issue #4 added the operator
 * public key at 0x80-0xBF) hold the bootloader's public-key hash, the device
 * serial and -- element tier only -- that operator key. Without this check,
 * the generic EEPROM primitive would let anyone on the serial line overwrite
 * the hash the bootloader verifies against -- the whole root of trust, defeated
 * by a documented command doing exactly what it says it does.
 */
static void test_the_reserved_region_cannot_be_written(void)
{
    const uint8_t evil[4] = { 0, 0, 0, 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x000u, evil, 4u)); /* pubkey hash */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x020u, evil, 4u)); /* device serial */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x030u, evil, 4u)); /* boot counter */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x044u, evil, 4u)); /* operator pubkey, issue #4 */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0BFu, evil, 1u)); /* last byte of it */
    /* and nothing was written: the region keeps the erased part's bytes */
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, tfw_mock_eeprom(), 0x0C0u);
}

/* A write beginning below the boundary and running past it must be refused
 * whole. Checking only the starting address would let a write at 0xBF running
 * eight bytes reach 0xC6 -- through the boundary, having passed the check. */
static void test_a_write_straddling_the_boundary_is_refused_entirely(void)
{
    const uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0BFu, data, 8u));
    /* and nothing was written, on either side of the boundary */
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, &tfw_mock_eeprom()[0x0BF], 8u);
}

static void test_the_first_byte_above_the_boundary_is_writable(void)
{
    const uint8_t one[1] = { 0x5A };
    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x0C0u, one, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0C0]);
}

/* The ATmega2560's bootloader keeps its swap journal at 0x0E0-0x0E9, above the
 * platform block, and its build refuses it (test_storage_2560.c). This board
 * declares no such range, so the same address is the application's. */
static void test_a_board_without_the_range_accepts_0x0E0(void)
{
    const uint8_t one[1] = { 0x5A };
    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x0E0u, one, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0E0]);
}

/* Reading the reserved region on THIS (soft-tier) build is still permitted
 * unconditionally: TFW_FEATURE_AUTH_SESSION is off here (HOST_EXTRA_DEFS does
 * not turn it on, and this binary links auth_session_none.c), so
 * tfw_auth_gate() is the permissive stub -- exactly the accepted soft-tier gap
 * docs/security/threat-model.md states. test_auth_session.c exercises the
 * element-tier build, where the same read is refused before authentication. */
static void test_the_reserved_region_can_be_read(void)
{
    tfw_mock_eeprom()[0x000] = 0xAB;
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x000u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xABu, outbuf[0]);
}

static void test_an_address_past_the_part_is_refused(void)
{
    const uint8_t one[1] = { 0 };
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_write(2000u, one, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_read(2000u, 1u));
}

/* A length that would run off the end must be refused, and the check is
 * written to avoid overflowing addr + len -- reachable from a malformed
 * request rather than theoretical on a uint16_t address. */
static void test_a_length_running_past_the_end_is_refused(void)
{
    const uint8_t data[8] = { 0 };
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_write(1020u, data, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_read(1020u, 8u));
    /* addr + len past 0xFFFF, which a check summing them would wrap to 4 */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_write(0xFFFCu, data, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_read(0xFFFCu, 8u));
}

/* Chunked so a bulk write cannot hold the main loop. A byte takes ~3.4 ms, so
 * 64 bytes would block for 220 ms -- long enough to miss a scheduled deadline. */
static void test_a_write_larger_than_the_chunk_is_refused(void)
{
    const uint8_t data[16] = { 0 };
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_write(0x100u, data, 16u));
}

static void test_a_zero_length_write_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(TFW_CMD_EEPROM_WRITE, (const uint8_t[]){ 0u, 1u }, 2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_round_trip_through_the_user_area);
    RUN_TEST(test_the_reserved_region_cannot_be_written);
    RUN_TEST(test_a_write_straddling_the_boundary_is_refused_entirely);
    RUN_TEST(test_the_first_byte_above_the_boundary_is_writable);
    RUN_TEST(test_a_board_without_the_range_accepts_0x0E0);
    RUN_TEST(test_the_reserved_region_can_be_read);
    RUN_TEST(test_an_address_past_the_part_is_refused);
    RUN_TEST(test_a_length_running_past_the_end_is_refused);
    RUN_TEST(test_a_write_larger_than_the_chunk_is_refused);
    RUN_TEST(test_a_zero_length_write_is_refused);
    return UNITY_END();
}
