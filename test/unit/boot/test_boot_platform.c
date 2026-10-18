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

static void build(uint8_t raw[TFW_BOOT_EE_LEN], tfw_boot_platform_t *p)
{
    memset(raw, 0xFF, TFW_BOOT_EE_LEN);
    memset(p, 0, sizeof *p);
    memset(p->pubkey_hash, 0x11, sizeof p->pubkey_hash);
    memset(p->serial, 0x22, sizeof p->serial);
    memset(p->operator_pubkey, 0x33, sizeof p->operator_pubkey);
    p->rollback_floor = 4u;
    p->boot_count     = 17u;
    p->boot_state     = TFW_BOOT_STATE_GOOD;
    p->ota_state      = TFW_BOOT_OTA_IDLE;
    p->fail_count     = 2u;
    tfw_boot_platform_serialise(p, raw);
}

/* Parses into a sentinel-filled *out and checks a refusal left every byte of
 * it as the caller passed it. Returns the parser's status. */
static tfw_status_t parse_untouched(const uint8_t *raw, tfw_boot_platform_t *out)
{
    tfw_boot_platform_t sentinel;
    tfw_status_t        st;

    memset(out, 0xA5, sizeof *out);
    memcpy(&sentinel, out, sizeof sentinel);
    st = tfw_boot_platform_parse(raw, out);
    TEST_ASSERT_NOT_EQUAL_INT(TFW_OK, st);
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, out, sizeof sentinel);
    return st;
}

static void test_round_trips_every_field(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t in, out;

    build(raw, &in);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_platform_parse(raw, &out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.pubkey_hash, out.pubkey_hash, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.serial, out.serial, TFW_BOOT_SERIAL_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.operator_pubkey, out.operator_pubkey,
                                   TFW_BOOT_OPERATOR_PUBKEY_LEN);
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
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t out;

    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, parse_untouched(raw, &out));
}

static void test_a_zeroed_region_reports_not_provisioned(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t out;

    memset(raw, 0x00, sizeof raw);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, parse_untouched(raw, &out));
}

static void test_an_unknown_layout_version_is_refused(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t in, out;

    build(raw, &in);
    raw[TFW_BOOT_EE_O_LAYOUT] = TFW_BOOT_EE_VERSION + 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, parse_untouched(raw, &out));
}

/* A corrupted rollback floor is a downgrade attack that arrives by accident.
 * This is how the bootloader NOTICES when BODLEVEL was not set. */
static void test_a_corrupted_byte_is_caught_by_the_crc(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t in, out;

    build(raw, &in);
    raw[TFW_BOOT_EE_O_ROLLBACK_FLOOR] ^= 0x08u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, parse_untouched(raw, &out));
}

static void test_a_corrupted_pubkey_hash_is_caught(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t in, out;

    build(raw, &in);
    raw[TFW_BOOT_EE_O_PUBKEY_HASH + 31u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, parse_untouched(raw, &out));
}

/*
 * The bootloader rewrites this region to record a boot count. Anything it does
 * not model -- the reserved span, and whatever a later layout puts there -- must
 * survive that, or a bootloader update silently erases what another tool owns.
 */
static void test_serialising_preserves_bytes_the_struct_does_not_model(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    build(raw, &p);
    raw[150] = 0xDEu;   /* inside the reserved span since issue #4 moved it
                         * (operator_pubkey now occupies what used to be
                         * unmodelled bytes 68-131) */
    raw[170] = 0xADu;
    tfw_boot_platform_serialise(&p, raw);   /* rewritten, as a boot would */

    TEST_ASSERT_EQUAL_UINT8(0xDEu, raw[150]);
    TEST_ASSERT_EQUAL_UINT8(0xADu, raw[170]);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_platform_parse(raw, &p));
}

/* The block is copied whole, so the multi-byte fields are only as right as the
 * target's byte order. These literals are the layout: little-endian, each field
 * at its offset. They are not computed from the code they check. */
static void test_multi_byte_fields_are_little_endian_at_their_offsets(void)
{
    static const uint8_t floor[4] = { 0x04u, 0x03u, 0x02u, 0x01u };
    static const uint8_t count[4] = { 0x0Du, 0x0Cu, 0x0Bu, 0x0Au };
    static const uint8_t fails[2] = { 0x06u, 0x05u };
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    build(raw, &p);
    p.rollback_floor = 0x01020304u;
    p.boot_count     = 0x0A0B0C0Du;
    p.fail_count     = 0x0506u;
    tfw_boot_platform_serialise(&p, raw);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(floor, &raw[40], 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(count, &raw[44], 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fails, &raw[50], 2);

    memset(&p, 0, sizeof p);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_platform_parse(raw, &p));
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, p.rollback_floor);
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0C0Du, p.boot_count);
    TEST_ASSERT_EQUAL_HEX16(0x0506u, p.fail_count);
}

static void test_the_crc_covers_everything_below_it(void)
{
    uint8_t raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;
    uint16_t before;

    build(raw, &p);
    before = tfw_boot_platform_crc(raw);
    raw[125] ^= 0xFFu;                     /* the last byte the CRC covers */
    TEST_ASSERT_NOT_EQUAL(before, tfw_boot_platform_crc(raw));
}

/*
 * A LITERAL, exactly as testdata/vectors.h's are -- the 192 bytes
 * `tfwprov --bake-eeprom --serial 000102030405060708090a0b0c0d0e0f --out
 * test.eep` actually wrote, decoded from its own Intel HEX output. This is
 * the cross-implementation check issue #6 asks for: sdk/go's
 * BuildPlatformBlock and this file's tfw_boot_platform_parse are two
 * implementations of one layout, and the only way to know they agree is to
 * run one's OUTPUT through the other's PARSER. A change to either side that
 * broke agreement would pass every test that only exercises one of them.
 *
 * Regenerated for layout_version 2 when issue #4 added operator_pubkey and
 * grew the block from 128 to 192 bytes -- the old 128-byte v1 vector is
 * gone rather than kept alongside this one, because it pinned a layout this
 * parser no longer accepts at all (TFW_ERR_UNSUPPORTED), not a value it could
 * still decode differently; there is nothing left for it to check.
 */
static void test_a_baked_eeprom_image_is_decoded_correctly(void)
{
    static const uint8_t raw[TFW_BOOT_EE_LEN] = {
        0x54u,0x46u,0x50u,0x52u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x01u,0x02u,0x03u,0x04u,0x05u,0x06u,0x07u,
        0x08u,0x09u,0x0Au,0x0Bu,0x0Cu,0x0Du,0x0Eu,0x0Fu,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0xD5u,0xADu
    };
    static const uint8_t want_serial[TFW_BOOT_SERIAL_LEN] = {
        0x00u,0x01u,0x02u,0x03u,0x04u,0x05u,0x06u,0x07u,
        0x08u,0x09u,0x0Au,0x0Bu,0x0Cu,0x0Du,0x0Eu,0x0Fu
    };
    tfw_boot_platform_t p;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_platform_parse(raw, &p));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_serial, p.serial, TFW_BOOT_SERIAL_LEN);
    /* A factory-fresh bake: nothing has booted yet. */
    TEST_ASSERT_EQUAL_UINT32(0u, p.rollback_floor);
    TEST_ASSERT_EQUAL_UINT32(0u, p.boot_count);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_EMPTY, p.boot_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_IDLE, p.ota_state);
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
    RUN_TEST(test_multi_byte_fields_are_little_endian_at_their_offsets);
    RUN_TEST(test_the_crc_covers_everything_below_it);
    RUN_TEST(test_a_baked_eeprom_image_is_decoded_correctly);
    return UNITY_END();
}
