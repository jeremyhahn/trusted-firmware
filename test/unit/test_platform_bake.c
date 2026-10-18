/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bench bake image's decision (tools/platform-bake/bake.c), run through the
 * mock HAL: what it writes, where, and what it says about it.
 *
 * WANT IS A LITERAL, and it is tfwprov's. It is the block
 *
 *   tfwprov --bake-eeprom --serial 0123456789abcdeffedcba9876543210
 *
 * writes, transcribed byte for byte from the .eep: magic "TFPR", layout 2, the
 * serial at 52, everything else zero, CRC 0x01CB at 190. A block built here
 * from boot_platform.c would agree with boot_platform.c whatever it did; this
 * one agrees with the encoder the bake image actually carries.
 */
#include <string.h>

#include "unity.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "bake.h"

static const uint8_t WANT[TFW_BOOT_EE_LEN] = {
    0x54, 0x46, 0x50, 0x52, 0x02,
    [52] = 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
           0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    [190] = 0xCB, 0x01,
};

/* What sits past the region: not 0xFF, so a write that strays is seen. */
#define SENTINEL 0xA5u

void setUp(void)
{
    tfw_mock_reset();
    memset(tfw_mock_eeprom() + TFW_BOOT_EE_LEN, SENTINEL,
           tfw_mock_eeprom_size() - TFW_BOOT_EE_LEN);
}
void tearDown(void) {}

static void assert_outside_untouched(void)
{
    const uint8_t *ee = tfw_mock_eeprom();
    size_t         i;

    for (i = TFW_BOOT_EE_LEN; i < tfw_mock_eeprom_size(); i++) {
        TEST_ASSERT_EQUAL_HEX8(SENTINEL, ee[i]);
    }
}

static void assert_nothing_written(void)
{
    uint16_t lo;
    uint16_t hi;

    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_eeprom_written(&lo, &hi));
}

/* WANT with a different serial and a CRC to match: a real identity, not
 * this one. */
static void other_valid_block(uint8_t out[TFW_BOOT_EE_LEN])
{
    tfw_boot_platform_t p;

    memcpy(out, WANT, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(out, &p));
    p.serial[TFW_BOOT_SERIAL_LEN - 1u] ^= 0xFFu;
    tfw_boot_platform_serialise(&p, out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(out, &p));
}

static void test_the_literal_is_a_block_the_firmware_accepts(void)
{
    tfw_boot_platform_t p;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_platform_parse(WANT, &p));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&WANT[TFW_BOOT_EE_O_SERIAL], p.serial,
                                 TFW_BOOT_SERIAL_LEN);
}

/* Exactly the 192 bytes of the region, and not one byte past it. */
static void test_a_blank_region_is_baked_with_exactly_192_bytes(void)
{
    uint16_t lo;
    uint16_t hi;

    TEST_ASSERT_EQUAL(TFW_BAKE_BAKED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_EE_BASE, lo);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_EE_BASE + TFW_BOOT_EE_LEN, hi);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, tfw_mock_eeprom() + TFW_BOOT_EE_BASE,
                                 TFW_BOOT_EE_LEN);
    assert_outside_untouched();
}

/* The Mega's region as the 2026-10-15 run found it: the first 128 bytes of a
 * block, the rest -- the CRC with it -- still erased. Not an identity, so it
 * is overwritten rather than refused. */
static void test_a_torn_region_is_baked(void)
{
    uint8_t            *ee = tfw_mock_eeprom();
    tfw_boot_platform_t p;
    uint16_t            lo;
    uint16_t            hi;

    memcpy(ee, WANT, 128u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, tfw_boot_platform_parse(ee, &p));

    TEST_ASSERT_EQUAL(TFW_BAKE_BAKED, tfw_bake_run(WANT));
    TEST_ASSERT_EQUAL_UINT(TFW_BOOT_EE_LEN, tfw_mock_eeprom_written(&lo, &hi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, ee, TFW_BOOT_EE_LEN);
    assert_outside_untouched();
}

/* A second run -- the DTR reset when a terminal opens the port -- finds its
 * own block and writes nothing: no wear, no window for a torn write. */
static void test_an_equal_block_is_already_and_writes_nothing(void)
{
    memcpy(tfw_mock_eeprom(), WANT, TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL(TFW_BAKE_ALREADY, tfw_bake_run(WANT));
    assert_nothing_written();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, tfw_mock_eeprom(), TFW_BOOT_EE_LEN);
    assert_outside_untouched();
}

/* A device that already has a different identity keeps it. */
static void test_a_different_valid_block_is_refused_and_left_alone(void)
{
    uint8_t other[TFW_BOOT_EE_LEN];

    other_valid_block(other);
    memcpy(tfw_mock_eeprom(), other, TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(WANT));
    assert_nothing_written();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(other, tfw_mock_eeprom(), TFW_BOOT_EE_LEN);
    assert_outside_untouched();
}

/* A cell that does not take the write: what comes back is not the block, and
 * the image says so rather than "baked". */
static void test_a_readback_that_disagrees_is_reported(void)
{
    tfw_mock_eeprom_stuck_at(TFW_BOOT_EE_O_SERIAL);

    TEST_ASSERT_EQUAL(TFW_BAKE_READBACK_FAILED, tfw_bake_run(WANT));
    assert_outside_untouched();
}

/* A region that cannot be read is not known to be blank: refused, unwritten. */
static void test_an_unreadable_region_is_refused_and_not_written(void)
{
    tfw_mock_eeprom_read_fails(1u);

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(WANT));
    assert_nothing_written();
}

/* A carried block the bootloader would reject is never written. */
static void test_an_invalid_carried_block_is_refused_before_any_write(void)
{
    uint8_t bad[TFW_BOOT_EE_LEN];

    memcpy(bad, WANT, TFW_BOOT_EE_LEN);
    bad[TFW_BOOT_EE_O_CRC] ^= 0x01u;

    TEST_ASSERT_EQUAL(TFW_BAKE_REFUSED, tfw_bake_run(bad));
    assert_nothing_written();
}

static void test_the_result_line(void)
{
    static const char want_line[] =
        "platform-bake board=atmega2560 serial=0123456789abcdeffedcba9876543210"
        " result=baked\r\n";
    uint8_t out[128];
    size_t  n;

    tfw_bake_report("atmega2560", TFW_BAKE_BAKED, WANT);
    n = tfw_mock_drain_tx(out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(sizeof want_line - 1u, n);
    TEST_ASSERT_EQUAL_MEMORY(want_line, out, n);
}

static void test_every_result_has_its_name(void)
{
    TEST_ASSERT_EQUAL_STRING("already",         tfw_bake_result_name(TFW_BAKE_ALREADY));
    TEST_ASSERT_EQUAL_STRING("refused",         tfw_bake_result_name(TFW_BAKE_REFUSED));
    TEST_ASSERT_EQUAL_STRING("baked",           tfw_bake_result_name(TFW_BAKE_BAKED));
    TEST_ASSERT_EQUAL_STRING("readback-failed", tfw_bake_result_name(TFW_BAKE_READBACK_FAILED));
    /* A value from a newer bake.h than this build reads as unrecognised. */
    TEST_ASSERT_EQUAL_STRING("unknown",         tfw_bake_result_name((tfw_bake_result_t)0x7F));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_literal_is_a_block_the_firmware_accepts);
    RUN_TEST(test_a_blank_region_is_baked_with_exactly_192_bytes);
    RUN_TEST(test_a_torn_region_is_baked);
    RUN_TEST(test_an_equal_block_is_already_and_writes_nothing);
    RUN_TEST(test_a_different_valid_block_is_refused_and_left_alone);
    RUN_TEST(test_a_readback_that_disagrees_is_reported);
    RUN_TEST(test_an_unreadable_region_is_refused_and_not_written);
    RUN_TEST(test_an_invalid_carried_block_is_refused_before_any_write);
    RUN_TEST(test_the_result_line);
    RUN_TEST(test_every_result_has_its_name);
    return UNITY_END();
}
