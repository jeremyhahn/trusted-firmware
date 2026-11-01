/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The platform-reserved EEPROM region.
 *
 * Every interesting case here is a corruption case, and none of them is
 * convenient to produce on a bench: a region that was never written, one whose
 * CRC no longer matches because the supply sagged mid-write, one from a layout
 * version this build does not know. test_boot_platform_mutate.c drives every
 * one of them, every byte at every value against a model of the parser; this
 * file keeps the round trip and the literal layout.
 */

#include <string.h>
#include "unity.h"
#include "boot_platform.h"

void setUp(void) {}
void tearDown(void) {}

static void build(uint8_t raw[TFW_BOOT_EE_BLOCK_LEN], tfw_boot_platform_t *p)
{
    memset(raw, 0xFF, TFW_BOOT_EE_BLOCK_LEN);
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

static void test_round_trips_every_field(void)
{
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];
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

/* The block is copied whole, so the multi-byte fields are only as right as the
 * target's byte order. These literals are the layout: little-endian, each field
 * at its offset. They are not computed from the code they check. */
static void test_multi_byte_fields_are_little_endian_at_their_offsets(void)
{
    static const uint8_t floor[4] = { 0x04u, 0x03u, 0x02u, 0x01u };
    static const uint8_t count[4] = { 0x0Du, 0x0Cu, 0x0Bu, 0x0Au };
    static const uint8_t fails[2] = { 0x06u, 0x05u };
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];
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
    uint8_t raw[TFW_BOOT_EE_BLOCK_LEN];
    tfw_boot_platform_t p;
    uint16_t before;
    uint16_t i;

    build(raw, &p);
    before = tfw_boot_platform_crc(raw);
    for (i = 0u; i < TFW_BOOT_EE_O_CRC; i++) {      /* every byte below the CRC */
        raw[i] ^= 0xFFu;
        TEST_ASSERT_NOT_EQUAL(before, tfw_boot_platform_crc(raw));
        raw[i] ^= 0xFFu;
    }
    for (i = TFW_BOOT_EE_O_CRC; i < TFW_BOOT_EE_BLOCK_LEN; i++) {   /* and not itself */
        raw[i] ^= 0xFFu;
        TEST_ASSERT_EQUAL_HEX16(before, tfw_boot_platform_crc(raw));
        raw[i] ^= 0xFFu;
    }
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
    static const uint8_t raw[TFW_BOOT_EE_BLOCK_LEN] = {
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
    RUN_TEST(test_multi_byte_fields_are_little_endian_at_their_offsets);
    RUN_TEST(test_the_crc_covers_everything_below_it);
    RUN_TEST(test_a_baked_eeprom_image_is_decoded_correctly);
    return UNITY_END();
}
