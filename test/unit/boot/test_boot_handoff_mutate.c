/*
 * boot/boot_handoff.c under mutation (#163): the block tfw-boot hands the
 * application -- its fields, its bounds, and the zeroing of whatever the
 * SRAM held before, which must not leak into the bytes the block does not
 * name.
 *
 * test_boot_measure round-trips the block and refuses the versions it does
 * not know. This suite pins the encoding byte for byte, holds the parse
 * against every single-bit damage, and checks that a refused block writes
 * nothing to the caller.
 *
 * Mutants this suite kills (each tried against the unit and seen to fail it).
 * test_boot_measure and test_appstate_mutate already kill H4-H32 between
 * them; H1-H3, the zeroing and its bound, only this suite kills:
 *   H1  build: the memset dropped (flags, reserved0 keep stale SRAM)
 *   H2  build: the memset three bytes short (reserved0 keeps stale SRAM)
 *   H3  build: the memset one byte long (writes past the block)
 *   H4  build: the magic not written
 *   H5  build: the version written as VERSION - 1
 *   H6  build: the tier not written
 *   H7  build: the CDI and the measurement at each other's offset
 *   H8  build: image_version not written
 *   H9  build: wr_u32's third byte shifted by 8, not 16
 *   H10 build: reset_cause not written
 *   H11 build: the CRC over [0, 77), not [0, 78)
 *   H12 build: the CRC's two bytes swapped
 *   H13 parse: the magic check dropped
 *   H14 parse: the magic compare inverted
 *   H15 parse: the version check dropped
 *   H16 parse: `!= VERSION` -> `< VERSION` (a newer version read)
 *   H17 parse: the CRC check dropped
 *   H18 parse: the received CRC's bytes swapped
 *   H19 parse: the CRC over [0, 77), so reserved0 goes unchecked
 *   H20 parse: the version checked before the magic (error swapped)
 *   H21 parse: the CRC checked before the version (error swapped)
 *   H22 parse: the outputs written before the checks
 *   H23 parse: the tier not copied out
 *   H24 parse: the CDI read from the measurement's offset
 *   H25 parse: image_version read from the CDI's offset
 *   H26 parse: reset_cause not copied out
 *   H27 parse: rd_u32's top byte shifted by 16, not 24
 *   H28 parse: a bad CRC reported as UNSUPPORTED
 *   H29 parse: a bad magic reported as UNSUPPORTED
 *   H30 parse: the measurement copied one byte short
 *   H31 build: the CDI copied one byte short
 *   H32 parse: a good block refused
 */
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "crc16.h"

/* tier 2, cdi 0x10.., measurement 0x80.., image_version 0x0A0B0C0D, reset
 * cause PANIC (6), the all-zero device seed a build with no UDS leaves,
 * CRC-16/CCITT-FALSE over [0, 78) and [80, 112) = 0xBA68 (version 4). A
 * literal, so a moved encoding fails here. */
static const uint8_t WIRE[TFW_BOOT_HANDOFF_LEN] = {
    0x41u, 0x43u, 0x44u, 0x49u, 0x04u, 0x02u, 0x00u, 0x00u,
    0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu,
    0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u,
    0x28u, 0x29u, 0x2Au, 0x2Bu, 0x2Cu, 0x2Du, 0x2Eu, 0x2Fu,
    0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u,
    0x88u, 0x89u, 0x8Au, 0x8Bu, 0x8Cu, 0x8Du, 0x8Eu, 0x8Fu,
    0x90u, 0x91u, 0x92u, 0x93u, 0x94u, 0x95u, 0x96u, 0x97u,
    0x98u, 0x99u, 0x9Au, 0x9Bu, 0x9Cu, 0x9Du, 0x9Eu, 0x9Fu,
    0x0Du, 0x0Cu, 0x0Bu, 0x0Au, 0x06u, 0x00u, 0x68u, 0xBAu,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
};

#define GUARD 8u

static uint8_t cdi_in[TFW_BOOT_MEASURE_LEN];
static uint8_t meas_in[TFW_BOOT_MEASURE_LEN];

void setUp(void)
{
    for (unsigned i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        cdi_in[i]  = (uint8_t)(0x10u + i);
        meas_in[i] = (uint8_t)(0x80u + i);
    }
}
void tearDown(void) {}

/* H1-H12: built over stale SRAM, the block is the literal and nothing past
 * it is touched. */
static void test_build_writes_the_literal_over_stale_sram_and_nothing_else(void)
{
    uint8_t region[GUARD + TFW_BOOT_HANDOFF_LEN + GUARD];
    uint8_t want[sizeof region];

    memset(region, 0xA5u, sizeof region);
    memset(want, 0xA5u, sizeof want);
    memcpy(&want[GUARD], WIRE, sizeof WIRE);
    tfw_boot_handoff_build(&region[GUARD], 2u, cdi_in, meas_in, 0x0A0B0C0DuL,
                           TFW_RESET_PANIC);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, region, sizeof region);
}

/* H18, H23-H27: the literal parses to exactly its fields, and no output is
 * written past its length. */
static void test_parse_reads_every_field_of_the_literal(void)
{
    uint8_t tier = 0xEEu, cause = 0xEEu;
    uint32_t ver = 0xEEEEEEEEuL;
    uint8_t cdi[TFW_BOOT_MEASURE_LEN + GUARD];
    uint8_t meas[TFW_BOOT_MEASURE_LEN + GUARD];
    uint8_t seed[TFW_BOOT_SEED_LEN + GUARD];
    uint8_t guard[GUARD];
    uint8_t zero_seed[TFW_BOOT_SEED_LEN];

    memset(cdi, 0xEEu, sizeof cdi);
    memset(meas, 0xEEu, sizeof meas);
    memset(seed, 0xEEu, sizeof seed);
    memset(guard, 0xEEu, sizeof guard);
    memset(zero_seed, 0, sizeof zero_seed);
    TEST_ASSERT_EQUAL(TFW_OK,
        tfw_boot_handoff_parse(WIRE, &tier, cdi, meas, &ver, &cause, seed));
    TEST_ASSERT_EQUAL_HEX8(2u, tier);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cdi_in, cdi, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(meas_in, meas, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(guard, &cdi[TFW_BOOT_MEASURE_LEN], GUARD);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(guard, &meas[TFW_BOOT_MEASURE_LEN], GUARD);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero_seed, seed, TFW_BOOT_SEED_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(guard, &seed[TFW_BOOT_SEED_LEN], GUARD);
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0C0DuL, ver);
    TEST_ASSERT_EQUAL_HEX8(TFW_RESET_PANIC, cause);
}

/* The status a single damaged bit earns: a magic that is not the magic is no
 * handoff, a version this build does not know is refused, and every other
 * byte -- reserved0 and the CRC itself included -- is caught by the CRC. */
static tfw_status_t want_for(unsigned off)
{
    if (off < TFW_BOOT_HO_VERSION) {
        return TFW_ERR_NOT_PROVISIONED;
    }
    if (off == TFW_BOOT_HO_VERSION) {
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_ERR_CRC;
}

/* H13-H17, H19, H22: every single-bit damage is refused with its own status,
 * and a refused block writes nothing to the caller. */
static void test_every_single_bit_damage_is_refused_and_writes_nothing(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[TFW_BOOT_MEASURE_LEN], meas[TFW_BOOT_MEASURE_LEN];
    uint8_t seed[TFW_BOOT_SEED_LEN];
    uint8_t untouched[TFW_BOOT_MEASURE_LEN];

    memset(untouched, 0xEEu, sizeof untouched);
    for (unsigned off = 0u; off < TFW_BOOT_HANDOFF_LEN; off++) {
        for (unsigned bit = 0u; bit < 8u; bit++) {
            uint8_t tier = 0xEEu, cause = 0xEEu;
            uint32_t ver = 0xEEEEEEEEuL;

            memcpy(block, WIRE, sizeof block);
            block[off] ^= (uint8_t)(1u << bit);
            memset(cdi, 0xEEu, sizeof cdi);
            memset(meas, 0xEEu, sizeof meas);
            memset(seed, 0xEEu, sizeof seed);
            TEST_ASSERT_EQUAL_MESSAGE(want_for(off),
                tfw_boot_handoff_parse(block, &tier, cdi, meas, &ver, &cause, seed),
                "status");
            TEST_ASSERT_EQUAL_HEX8(0xEEu, tier);
            TEST_ASSERT_EQUAL_HEX8(0xEEu, cause);
            TEST_ASSERT_EQUAL_HEX32(0xEEEEEEEEuL, ver);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(untouched, cdi, sizeof cdi);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(untouched, meas, sizeof meas);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(untouched, seed, sizeof seed);
        }
    }
}

/* H16: a newer version is refused exactly like an older one, even with a CRC
 * that is good for it. */
static void test_a_newer_version_with_a_good_crc_is_refused(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t tier, cause, cdi[TFW_BOOT_MEASURE_LEN], meas[TFW_BOOT_MEASURE_LEN];
    uint8_t seed[TFW_BOOT_SEED_LEN];
    uint32_t ver;

    tfw_boot_handoff_build(block, 2u, cdi_in, meas_in, 1u, TFW_RESET_UNKNOWN);
    block[TFW_BOOT_HO_VERSION] = (uint8_t)(TFW_BOOT_HANDOFF_VERSION + 1u);
    {
        uint16_t crc = tfw_boot_handoff_crc(block);
        block[TFW_BOOT_HO_CRC]      = (uint8_t)(crc & 0xFFu);
        block[TFW_BOOT_HO_CRC + 1u] = (uint8_t)(crc >> 8);
    }
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
        tfw_boot_handoff_parse(block, &tier, cdi, meas, &ver, &cause, seed));
}

/* H20, H21: the checks run in order -- magic, then version, then CRC -- so a
 * block wrong in several ways is reported by the first. */
static void test_the_checks_run_magic_then_version_then_crc(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t tier, cause, cdi[TFW_BOOT_MEASURE_LEN], meas[TFW_BOOT_MEASURE_LEN];
    uint8_t seed[TFW_BOOT_SEED_LEN];
    uint32_t ver;

    memcpy(block, WIRE, sizeof block);
    block[TFW_BOOT_HO_MAGIC] ^= 0x01u;
    block[TFW_BOOT_HO_VERSION] ^= 0x01u;
    block[TFW_BOOT_HO_CRC] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED,
        tfw_boot_handoff_parse(block, &tier, cdi, meas, &ver, &cause, seed));

    block[TFW_BOOT_HO_MAGIC] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
        tfw_boot_handoff_parse(block, &tier, cdi, meas, &ver, &cause, seed));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_writes_the_literal_over_stale_sram_and_nothing_else);
    RUN_TEST(test_parse_reads_every_field_of_the_literal);
    RUN_TEST(test_every_single_bit_damage_is_refused_and_writes_nothing);
    RUN_TEST(test_a_newer_version_with_a_good_crc_is_refused);
    RUN_TEST(test_the_checks_run_magic_then_version_then_crc);
    return UNITY_END();
}
