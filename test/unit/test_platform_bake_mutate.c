/*
 * Mutation tests for the bench bake image's decision, tools/platform-bake/bake.c
 * (issue #163).
 *
 * test_platform_bake drives the same unit through the mock HAL, which cannot
 * fail a write, fail only the read-back, hand back a valid block other than
 * the one written, or refuse a UART byte part-way through a line. This file
 * links bake.c against its own three HAL functions instead, so each of those
 * paths is reached and a mutant that drops its check is seen.
 */
#include <string.h>

#include "unity.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "bake.h"

static const uint8_t WANT[TFW_BOOT_EE_LEN] = {
    0x54, 0x46, 0x50, 0x52, 0x02,
    [52] = 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
           0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    [190] = 0xCB, 0x01,
};

/* ------------------------------------------------- the three HAL functions */

static uint8_t        ee[TFW_BOOT_EE_LEN];
static unsigned       reads;
static unsigned       read_fails_at;      /* the 1-based read that fails; 0 none */
static uint16_t       first_read_addr;
static uint16_t       read_addr;
static uint8_t        read_len;
static unsigned       writes;
static uint16_t       write_addr;
static uint8_t        write_len;
static tfw_status_t   write_status;
static const uint8_t *write_lands;        /* what a write leaves; NULL = buf */

static char     tx[160];
static size_t   tx_len;
static unsigned tx_tries;
static uint8_t  tx_refuse_odd;            /* refuse every other byte offered */

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    reads++;
    if (reads == 1u) {
        first_read_addr = addr;
    }
    read_addr = addr;
    read_len  = len;
    /* A failed read still leaves bytes in the buffer -- whatever the bus
     * returned. Only the status says they are not the region, so only the
     * status check stops them being used. */
    memcpy(buf, ee, len);
    return reads == read_fails_at ? TFW_ERR_TIMEOUT : TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    writes++;
    write_addr = addr;
    write_len  = len;
    memcpy(ee, write_lands != NULL ? write_lands : buf, len);
    return write_status;
}

uint8_t tfw_hal_uart_tx_try(uint8_t byte)
{
    tx_tries++;
    if (tx_refuse_odd && (tx_tries & 1u) != 0u) {
        return 0u;
    }
    tx[tx_len++] = (char)byte;
    return 1u;
}

void setUp(void)
{
    memset(ee, 0xFF, sizeof ee);
    reads = 0u;
    read_fails_at = 0u;
    first_read_addr = 0xFFFFu;
    read_addr = 0xFFFFu;
    read_len = 0u;
    writes = 0u;
    write_addr = 0xFFFFu;
    write_len = 0u;
    write_status = TFW_OK;
    write_lands = NULL;
    memset(tx, 0, sizeof tx);
    tx_len = 0u;
    tx_tries = 0u;
    tx_refuse_odd = 0u;
}
void tearDown(void) {}

/* WANT with a different serial and a CRC to match. */
static void other_valid_block(uint8_t out[TFW_BOOT_EE_LEN])
{
    tfw_boot_platform_t p;

    memcpy(out, WANT, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(out, &p));
    p.serial[0] ^= 0x5Au;
    tfw_boot_platform_serialise(&p, out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(out, &p));
}

/* ------------------------------------------------------------------ run */

static void test_a_blank_region_is_read_written_and_read_back_whole(void)
{
    TEST_ASSERT_EQUAL(TFW_BAKE_BAKED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(2u, reads);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_EE_BASE, first_read_addr);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_EE_BASE, read_addr);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN, read_len);
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_EE_BASE, write_addr);
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN, write_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, ee, TFW_BOOT_EE_LEN);
}

/* A write the part reports failed is not "baked", even if the cells took it --
 * and nothing is read back after it. */
static void test_a_write_that_reports_failure_is_readback_failed(void)
{
    write_status = TFW_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL(TFW_BAKE_READBACK_FAILED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_UINT(1u, reads);
}

/* The write lands, but the read-back cannot be made: not known to be baked. */
static void test_a_readback_that_cannot_be_read_is_readback_failed(void)
{
    read_fails_at = 2u;

    TEST_ASSERT_EQUAL(TFW_BAKE_READBACK_FAILED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(1u, writes);
    TEST_ASSERT_EQUAL_UINT(2u, reads);
}

/* What comes back is a valid identity -- just not this one. Only the byte
 * comparison can tell; the parse alone would call it baked. */
static void test_a_readback_of_another_valid_block_is_readback_failed(void)
{
    uint8_t other[TFW_BOOT_EE_LEN];

    other_valid_block(other);
    write_lands = other;

    TEST_ASSERT_EQUAL(TFW_BAKE_READBACK_FAILED, tfw_bake_run(WANT));
}

/* The first read failing refuses before any write, and reads no more. */
static void test_an_unreadable_region_is_refused_after_one_read(void)
{
    read_fails_at = 1u;

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(1u, reads);
    TEST_ASSERT_EQUAL_UINT(0u, writes);
}

/* An invalid carried block is refused before the region is even read. */
static void test_an_invalid_carried_block_is_refused_before_any_read(void)
{
    uint8_t bad[TFW_BOOT_EE_LEN];

    memcpy(bad, WANT, TFW_BOOT_EE_LEN);
    bad[TFW_BOOT_EE_O_MAGIC] ^= 0x01u;

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(bad));
    TEST_ASSERT_EQUAL_UINT(0u, reads);
    TEST_ASSERT_EQUAL_UINT(0u, writes);
}

static void test_its_own_block_is_already_after_one_read(void)
{
    memcpy(ee, WANT, TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL(TFW_BAKE_ALREADY, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(1u, reads);
    TEST_ASSERT_EQUAL_UINT(0u, writes);
}

static void test_another_identity_is_refused_after_one_read(void)
{
    other_valid_block(ee);

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(1u, reads);
    TEST_ASSERT_EQUAL_UINT(0u, writes);
}

/* --------------------------------------------------------------- report */

/* A UART that refuses every other byte still carries the whole line, in
 * order: a refused byte is offered again, never dropped. */
static void test_a_busy_uart_still_gets_the_whole_line(void)
{
    static const char want_line[] =
        "platform-bake board=atmega328p serial=0123456789abcdeffedcba9876543210"
        " result=readback-failed\r\n";

    tx_refuse_odd = 1u;
    tfw_bake_report("atmega328p", TFW_BAKE_READBACK_FAILED, WANT);

    TEST_ASSERT_EQUAL_UINT(sizeof want_line - 1u, tx_len);
    TEST_ASSERT_EQUAL_MEMORY(want_line, tx, tx_len);
    TEST_ASSERT_EQUAL_UINT(2u * (sizeof want_line - 1u), tx_tries);
}

/* Every hex digit, both nibbles, and the serial's first and last bytes. */
static void test_the_serial_is_every_byte_in_lowercase_hex(void)
{
    static const char want_line[] =
        "platform-bake board=x serial=00ff10f0a55a0f01e1d2c3b4968778ff"
        " result=already\r\n";
    static const uint8_t serial[TFW_BOOT_SERIAL_LEN] = {
        0x00, 0xFF, 0x10, 0xF0, 0xA5, 0x5A, 0x0F, 0x01,
        0xE1, 0xD2, 0xC3, 0xB4, 0x96, 0x87, 0x78, 0xFF,
    };
    uint8_t block[TFW_BOOT_EE_LEN];

    memset(block, 0xEE, sizeof block);
    memcpy(&block[TFW_BOOT_EE_O_SERIAL], serial, sizeof serial);
    tfw_bake_report("x", TFW_BAKE_ALREADY, block);

    TEST_ASSERT_EQUAL_UINT(sizeof want_line - 1u, tx_len);
    TEST_ASSERT_EQUAL_MEMORY(want_line, tx, tx_len);
}

static void test_each_result_reads_its_own_name(void)
{
    TEST_ASSERT_EQUAL_STRING("baked", tfw_bake_result_name(TFW_BAKE_BAKED));
    TEST_ASSERT_EQUAL_STRING("unknown",
                             tfw_bake_result_name((tfw_bake_result_t)(TFW_BAKE_READBACK_FAILED + 1)));
    tfw_bake_report("b", TFW_BAKE_REFUSED, WANT);
    TEST_ASSERT_NOT_NULL(strstr(tx, " result=refused\r\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_blank_region_is_read_written_and_read_back_whole);
    RUN_TEST(test_a_write_that_reports_failure_is_readback_failed);
    RUN_TEST(test_a_readback_that_cannot_be_read_is_readback_failed);
    RUN_TEST(test_a_readback_of_another_valid_block_is_readback_failed);
    RUN_TEST(test_an_unreadable_region_is_refused_after_one_read);
    RUN_TEST(test_an_invalid_carried_block_is_refused_before_any_read);
    RUN_TEST(test_its_own_block_is_already_after_one_read);
    RUN_TEST(test_another_identity_is_refused_after_one_read);
    RUN_TEST(test_a_busy_uart_still_gets_the_whole_line);
    RUN_TEST(test_the_serial_is_every_byte_in_lowercase_hex);
    RUN_TEST(test_each_result_reads_its_own_name);
    return UNITY_END();
}
