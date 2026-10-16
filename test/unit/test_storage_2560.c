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
    TEST_ASSERT_EQUAL_UINT(10u, TFW_BOOT_EE_EXTRA_LEN);
}

static void test_each_end_of_the_journal_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0E0u, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_write(0x0E9u, 1u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E9]);
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
    TEST_ASSERT_EQUAL(TFW_OK, ee_write(0x0EAu, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0DF]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_mock_eeprom()[0x0EA]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0E0]);
}

/* A read of the journal takes the platform block's gate; one beside it does not. */
static void test_a_read_of_the_journal_takes_the_gate(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, ee_read(0x0E9u, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, gate_calls);
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x0EAu, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, ee_read(0x0DFu, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, gate_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_journal_lies_past_the_platform_block);
    RUN_TEST(test_each_end_of_the_journal_is_refused);
    RUN_TEST(test_a_write_over_either_edge_is_refused);
    RUN_TEST(test_the_bytes_either_side_are_writable);
    RUN_TEST(test_a_read_of_the_journal_takes_the_gate);
    return UNITY_END();
}
