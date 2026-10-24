/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * EEPROM on the ATmega2560's profile, whose bootloader keeps the swap-move
 * journal at 0x0E0-0x0E9 -- above TFW_BOOT_EE_LEN, the bound the wire states
 * as EEPROMReservedEnd. The generic write must refuse every byte of it, with
 * the status the platform block's refusal already returns, and a read of it
 * takes the block's gate. test_storage.c pins a board without the range.
 */

#include <string.h>
#include "unity.h"
#include "storage.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "../core/auth_session.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;
static unsigned     gate_calls;

#define CHUNK 8u   /* src/caps/storage.c's TFW_EEPROM_CHUNK, the most one write takes */

/* The reserved region's read gate, supplied here in place of either
 * auth_session file so the suite sees which reads take it. */
tfw_status_t tfw_auth_gate(void)
{
    gate_calls++;
    return TFW_ERR_PROTECTED_REGION;
}

void setUp(void)    { tfw_mock_reset(); gate_calls = 0u; }
void tearDown(void) {}

static tfw_status_t ee_write(uint16_t addr, uint8_t n)
{
    uint8_t p[2 + CHUNK];

    p[0] = (uint8_t)(addr & 0xFFu);
    p[1] = (uint8_t)(addr >> 8);
    memset(&p[2], 0x5A, n);
    memcpy(inbuf, p, 2u + n);
    tfw_reader_init(&r, inbuf, (uint8_t)(2u + n));
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_storage_cmd_write(&r, &w);
}

static tfw_status_t ee_read(uint16_t addr, uint8_t n)
{
    inbuf[0] = (uint8_t)(addr & 0xFFu);
    inbuf[1] = (uint8_t)(addr >> 8);
    inbuf[2] = n;
    tfw_reader_init(&r, inbuf, 3u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_storage_cmd_read(&r, &w);
}

/* The journal sits past the wire's bound, or this suite tests nothing. */
static void test_the_journal_lies_past_the_platform_block(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0C0u, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL_HEX16(0x0E0u, TFW_BOOT_EE_EXTRA_BASE);
    TEST_ASSERT_EQUAL_UINT(21u, TFW_BOOT_EE_EXTRA_LEN);   /* the journal, then OTA_CONFIRM's request */
    TEST_ASSERT_EQUAL_HEX16(0x0EAu, TFW_BOOT_CONFIRM_ADDR);
}

static void test_each_end_of_the_journal_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0E0u, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0E9u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E9]);
    /* OTA_CONFIRM's request record (#77) is tfw-boot's to take, never the host's. */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0EAu, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0F4u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0F4]);
}

/* Refused whole: a write is checked over every byte it reaches, not its start. */
static void test_a_write_over_either_edge_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0DFu, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0E9u, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write((uint16_t)(0x0E1u - CHUNK), CHUNK));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0DF]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0EA]);
}

static void test_the_bytes_either_side_are_writable(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x0DFu, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x0F5u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0DF]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0F5]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E0]);
}

/* A read of the journal takes the platform block's gate; one beside it does not. */
static void test_a_read_of_the_journal_takes_the_gate(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_read(0x0E9u, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, gate_calls);
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x0F5u, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x0DFu, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, gate_calls);
}

/* A read wider than one chunk is refused before the gate is asked or a byte
 * of the part is copied out. */
static void test_a_read_longer_than_a_chunk_is_refused_and_nothing_is_returned(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_read(0x100u, (uint8_t)(CHUNK + 1u)));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT(0u, gate_calls);
}

/* The platform block below 0x0C0 takes the gate on a read too, and a refused
 * gate returns its own status with nothing read out. */
static void test_a_read_of_the_platform_block_is_refused_by_the_gate(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_read(0x000u, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, gate_calls);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

/* A part that fails to read passes its status up and returns no bytes. */
static void test_a_failed_part_read_is_passed_up_and_nothing_is_returned(void)
{
    tfw_mock_eeprom_read_fails(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, ee_read(0x100u, 4u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    tfw_mock_eeprom_read_fails(0u);
}

/* The handler itself refuses a write that carries an address and no data,
 * whatever the dispatcher's own argument count would have said. */
static void test_a_write_with_no_data_is_refused_and_nothing_is_written(void)
{
    uint16_t lo;
    uint16_t hi;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, ee_write(0x100u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

/* A write of nine bytes is one past the chunk: refused, nothing written. */
static void test_a_write_longer_than_a_chunk_is_refused_and_nothing_is_written(void)
{
    uint16_t lo;
    uint16_t hi;

    inbuf[0] = 0x00u;
    inbuf[1] = 0x01u;
    memset(&inbuf[2], 0x5A, CHUNK + 1u);
    tfw_reader_init(&r, inbuf, (uint8_t)(2u + CHUNK + 1u));
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_storage_cmd_write(&r, &w));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x100]);
}

/* A request too short to carry an address reads as address 0, which is the
 * platform block's: refused there, so the blob read after it is never reached. */
static void test_a_write_too_short_for_an_address_is_refused_and_nothing_is_written(void)
{
    uint16_t lo;
    uint16_t hi;

    inbuf[0] = 0x00u;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_storage_cmd_write(&r, &w));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_journal_lies_past_the_platform_block);
    RUN_TEST(test_each_end_of_the_journal_is_refused);
    RUN_TEST(test_a_write_over_either_edge_is_refused);
    RUN_TEST(test_the_bytes_either_side_are_writable);
    RUN_TEST(test_a_read_of_the_journal_takes_the_gate);
    RUN_TEST(test_a_read_longer_than_a_chunk_is_refused_and_nothing_is_returned);
    RUN_TEST(test_a_read_of_the_platform_block_is_refused_by_the_gate);
    RUN_TEST(test_a_failed_part_read_is_passed_up_and_nothing_is_returned);
    RUN_TEST(test_a_write_with_no_data_is_refused_and_nothing_is_written);
    RUN_TEST(test_a_write_longer_than_a_chunk_is_refused_and_nothing_is_written);
    RUN_TEST(test_a_write_too_short_for_an_address_is_refused_and_nothing_is_written);
    return UNITY_END();
}
