/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The platform-reserved EEPROM region.
 *
 * Every interesting case here is a corruption case, and none of them is
 * convenient to produce on a bench: a region that was never written, one whose
 * CRC no longer matches because the supply sagged mid-write, one from a layout
 * version this build does not know.
 */

#include <string.h>
#include "unity.h"
#include "boot_platform.h"

void setUp(void) {}
void tearDown(void) {}

static void build(uint8_t raw[AG_BOOT_EE_LEN], ag_boot_platform_t *p)
{
    memset(raw, 0xFF, AG_BOOT_EE_LEN);
    memset(p, 0, sizeof *p);
    memset(p->pubkey_hash, 0x11, sizeof p->pubkey_hash);
    memset(p->serial, 0x22, sizeof p->serial);
    p->rollback_floor = 4u;
    p->boot_count     = 17u;
    p->boot_state     = AG_BOOT_STATE_GOOD;
    p->ota_state      = AG_BOOT_OTA_IDLE;
    p->fail_count     = 2u;
    ag_boot_platform_serialise(p, raw);
}

static void test_round_trips_every_field(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t in, out;

    build(raw, &in);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_platform_parse(raw, &out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.pubkey_hash, out.pubkey_hash, AG_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.serial, out.serial, AG_BOOT_SERIAL_LEN);
    TEST_ASSERT_EQUAL_UINT32(in.rollback_floor, out.rollback_floor);
    TEST_ASSERT_EQUAL_UINT32(in.boot_count, out.boot_count);
    TEST_ASSERT_EQUAL_UINT8(in.boot_state, out.boot_state);
    TEST_ASSERT_EQUAL_UINT16(in.fail_count, out.fail_count);
}

/* "Never provisioned" and "provisioned and since corrupted" call for opposite
 * responses -- one is a device that never went through the factory, the other
 * is a device whose supply sagged during an EEPROM write. A bootloader that
 * reported both the same way would send a field failure back to the factory. */
static void test_a_blank_region_reports_not_provisioned(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t out;

    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT_EQUAL_INT(AG_ERR_NOT_PROVISIONED, ag_boot_platform_parse(raw, &out));
}

static void test_a_zeroed_region_reports_not_provisioned(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t out;

    memset(raw, 0x00, sizeof raw);
    TEST_ASSERT_EQUAL_INT(AG_ERR_NOT_PROVISIONED, ag_boot_platform_parse(raw, &out));
}

static void test_an_unknown_layout_version_is_refused(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t in, out;

    build(raw, &in);
    raw[AG_BOOT_EE_O_LAYOUT] = AG_BOOT_EE_VERSION + 1u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_UNSUPPORTED, ag_boot_platform_parse(raw, &out));
}

/* A corrupted rollback floor is a downgrade attack that arrives by accident.
 * This is how the bootloader NOTICES when BODLEVEL was not set. */
static void test_a_corrupted_byte_is_caught_by_the_crc(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t in, out;

    build(raw, &in);
    raw[AG_BOOT_EE_O_ROLLBACK_FLOOR] ^= 0x08u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_CRC, ag_boot_platform_parse(raw, &out));
}

static void test_a_corrupted_pubkey_hash_is_caught(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t in, out;

    build(raw, &in);
    raw[AG_BOOT_EE_O_PUBKEY_HASH + 31u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_CRC, ag_boot_platform_parse(raw, &out));
}

/*
 * The bootloader rewrites this region to record a boot count. Anything it does
 * not model -- the reserved span, and whatever a later layout puts there -- must
 * survive that, or a bootloader update silently erases what another tool owns.
 */
static void test_serialising_preserves_bytes_the_struct_does_not_model(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t p;

    build(raw, &p);
    raw[100] = 0xDEu;
    raw[110] = 0xADu;
    ag_boot_platform_serialise(&p, raw);   /* rewritten, as a boot would */

    TEST_ASSERT_EQUAL_UINT8(0xDEu, raw[100]);
    TEST_ASSERT_EQUAL_UINT8(0xADu, raw[110]);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_platform_parse(raw, &p));
}

static void test_the_crc_covers_everything_below_it(void)
{
    uint8_t raw[AG_BOOT_EE_LEN];
    ag_boot_platform_t p;
    uint16_t before;

    build(raw, &p);
    before = ag_boot_platform_crc(raw);
    raw[125] ^= 0xFFu;                     /* the last byte the CRC covers */
    TEST_ASSERT_NOT_EQUAL(before, ag_boot_platform_crc(raw));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trips_every_field);
    RUN_TEST(test_a_blank_region_reports_not_provisioned);
    RUN_TEST(test_a_zeroed_region_reports_not_provisioned);
    RUN_TEST(test_an_unknown_layout_version_is_refused);
    RUN_TEST(test_a_corrupted_byte_is_caught_by_the_crc);
    RUN_TEST(test_a_corrupted_pubkey_hash_is_caught);
    RUN_TEST(test_serialising_preserves_bytes_the_struct_does_not_model);
    RUN_TEST(test_the_crc_covers_everything_below_it);
    return UNITY_END();
}
