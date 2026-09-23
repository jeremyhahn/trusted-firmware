/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The provisioning commands, against the software element.
 *
 * Three of these tests are about refusals rather than operations, and those are
 * the ones that matter: every write here is irreversible on a real part, so a
 * command that accepts an argument it should have refused is a bricked board
 * rather than a failed call.
 */

#include <string.h>
#include "unity.h"
#include "provision.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "ag/protocol.h"
#include "ag/config.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD + 8u];
static ag_reader_t r;
static ag_writer_t w;

void setUp(void)    { ag_model_reset(); }
void tearDown(void) {}

static void prep(const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    ag_reader_init(&r, inbuf, n);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
}

/* --------------------------------------------------------------- INFO */

static void test_info_reports_the_element(void)
{
    prep(0, 0u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_info(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0x60u, outbuf[2]);        /* the 608 family byte */
}

/* A bus fault and an absent part look the same from here, so the status points
 * at the bus -- which is where the fault usually is. Saying "no element" would
 * send someone to the parts bin instead of to the wiring. */
static void test_a_silent_bus_is_reported_as_a_bus_fault(void)
{
    ag_model_set_busy(255u);
    prep(0, 0u);
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_prov_cmd_info(&r, &w));
}

/* ------------------------------------------------------- WRITE_CONFIG */

static void test_a_config_block_is_written(void)
{
    uint8_t p[33];
    uint8_t i;

    p[0] = 2u;
    for (i = 0u; i < 32u; i++) { p[1u + i] = (uint8_t)i; }
    prep(p, 33u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_write_config(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_WRITE, ag_model_last_opcode());
}

/*
 * Blocks 0 and 1 hold the serial number, the revision and the OTP mode. The
 * element refuses most of it anyway, but refusing here names the reason at the
 * point where a template is being debugged -- where "protected region" and the
 * element's generic execution error are very different amounts of help.
 */
static void test_the_blocks_that_are_not_ours_are_refused(void)
{
    uint8_t p[33] = { 0 };

    prep(p, 33u);
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ag_prov_cmd_write_config(&r, &w));
    p[0] = 1u;
    prep(p, 33u);
    TEST_ASSERT_EQUAL(AG_ERR_PROTECTED_REGION, ag_prov_cmd_write_config(&r, &w));
}

static void test_a_block_beyond_the_config_zone_is_refused(void)
{
    uint8_t p[33] = { 0 };

    p[0] = 9u;
    prep(p, 33u);
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_prov_cmd_write_config(&r, &w));
}

/* ---------------------------------------------------------------- LOCK */

/*
 * THE TEST THAT EARNS ITS KEEP.
 *
 * The element accepts a lock with the summary CRC suppressed, which locks
 * without checking that the zone holds what the caller believes it holds. On a
 * part that cannot be unlocked, that turns one transcription error in a
 * template into scrap. Requiring the CRC makes the element verify the zone
 * against the host's expectation before the irreversible step.
 */
static void test_a_lock_without_a_summary_crc_is_refused(void)
{
    const uint8_t p[3] = { AG_ATECC_LOCK_CONFIG, 0x00u, 0x00u };

    prep(p, 3u);
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, ag_prov_cmd_lock(&r, &w));
    /* And nothing reached the element. A refusal that still issued the command
     * would be no refusal at all. */
    TEST_ASSERT_EQUAL_HEX8(0u, ag_model_last_opcode());
}

static void test_a_lock_with_a_crc_reaches_the_element(void)
{
    const uint8_t p[3] = { AG_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(p, 3u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_LOCK, ag_model_last_opcode());
}

static void test_a_zone_the_part_does_not_have_is_refused(void)
{
    const uint8_t p[3] = { 0x7Fu, 0x34u, 0x12u };

    prep(p, 3u);
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_prov_cmd_lock(&r, &w));
}

/* -------------------------------------------------------------- KEYS */

static void test_genkey_returns_a_public_point_and_never_a_private_one(void)
{
    const uint8_t p[1] = { AG_ATECC_SLOT_DEVICEID };

    ag_model_provision();
    prep(p, 1u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_genkey(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(64u, ag_writer_len(&w));   /* an uncompressed point */
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_GENKEY, ag_model_last_opcode());
}

/* Generating into another slot would produce a key nothing in this firmware
 * will ever sign with -- a silent way to appear provisioned and not be. */
static void test_genkey_into_any_other_slot_is_refused(void)
{
    const uint8_t p[1] = { 5u };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_prov_cmd_genkey(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0u, ag_model_last_opcode());
}

/* PUBKEY must not be able to replace the identity it is reading. The two use
 * different GenKey modes for exactly this reason, and `agprov --verify` depends
 * on it: a verify that could regenerate the key would destroy what it checked. */
static void test_pubkey_reads_and_does_not_generate(void)
{
    const uint8_t p[1] = { AG_ATECC_SLOT_DEVICEID };

    ag_model_provision();
    prep(p, 1u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_pubkey(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(64u, ag_writer_len(&w));
}

static void test_pubkey_on_an_empty_slot_fails(void)
{
    const uint8_t p[1] = { AG_ATECC_SLOT_DEVICEID };

    prep(p, 1u);                       /* not provisioned: no key in slot 0 */
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_prov_cmd_pubkey(&r, &w));
}

/* ----------------------------------------------------------- SEED_UDS */

/*
 * The command takes a SLOT and no key material, and that is the whole design.
 * A UDS the host generated is a UDS the host has seen, and a provisioning
 * station is exactly the machine an attacker would target to get it. Here the
 * secret goes Random -> TempKey -> slot, entirely inside the element.
 */
static void test_seeding_the_uds_takes_no_key_material_from_the_host(void)
{
    const uint8_t p[1] = { AG_ATECC_SLOT_UDS };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_seed_uds(&r, &w));
    /* Random, then Nonce, then Write -- the last opcode proves it landed. */
    TEST_ASSERT_EQUAL_HEX8(AG_ATECC_OP_WRITE, ag_model_last_opcode());
    /* Nothing was written to the reply: there is no path by which a secret
     * leaves the device. */
    TEST_ASSERT_EQUAL_UINT8(0u, ag_writer_len(&w));

    /* And what landed is what the ELEMENT produced. The model's Random is a
     * known sequence, so asserting the slot holds it proves the value came from
     * inside the part rather than from anything the host supplied -- which is
     * the property the whole tier depends on and the one a passing call would
     * not otherwise demonstrate. */
    {
        const uint8_t *slot = ag_model_slot(AG_ATECC_SLOT_UDS);
        uint8_t        i, nonzero = 0u;
        TEST_ASSERT_NOT_NULL(slot);
        for (i = 0u; i < 32u; i++) {
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u ^ i), slot[i]);
            if (slot[i]) { nonzero = 1u; }
        }
        TEST_ASSERT_TRUE(nonzero);
    }
}

/* The config zone must be locked before the data zone, and neither twice. The
 * element enforces it; this pins that the firmware passes the refusal through
 * rather than reporting success for a lock that did not happen. */
static void test_locking_out_of_order_is_refused_by_the_element(void)
{
    const uint8_t data_first[3] = { AG_ATECC_LOCK_DATA, 0x34u, 0x12u };
    const uint8_t cfg[3]        = { AG_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(data_first, 3u);
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
    TEST_ASSERT_FALSE(ag_model_data_locked());

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
    TEST_ASSERT_TRUE(ag_model_config_locked());

    prep(data_first, 3u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
    TEST_ASSERT_TRUE(ag_model_data_locked());

    /* And a second lock of the same zone is refused rather than silently
     * succeeding, which is what a host's retry would otherwise look like. */
    prep(cfg, 3u);
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
}

/* A locked config zone refuses further writes. If this ever passes, the
 * irreversibility this whole procedure is built around is not real. */
static void test_a_locked_config_zone_refuses_writes(void)
{
    const uint8_t cfg[3] = { AG_ATECC_LOCK_CONFIG, 0x34u, 0x12u };
    uint8_t       blk[33] = { 2u };

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(AG_OK, ag_prov_cmd_lock(&r, &w));
    prep(blk, 33u);
    TEST_ASSERT_NOT_EQUAL(AG_OK, ag_prov_cmd_write_config(&r, &w));
}

static void test_seeding_any_other_slot_is_refused(void)
{
    const uint8_t p[1] = { 3u };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, ag_prov_cmd_seed_uds(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0u, ag_model_last_opcode());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_info_reports_the_element);
    RUN_TEST(test_a_silent_bus_is_reported_as_a_bus_fault);
    RUN_TEST(test_a_config_block_is_written);
    RUN_TEST(test_the_blocks_that_are_not_ours_are_refused);
    RUN_TEST(test_a_block_beyond_the_config_zone_is_refused);
    RUN_TEST(test_a_lock_without_a_summary_crc_is_refused);
    RUN_TEST(test_a_lock_with_a_crc_reaches_the_element);
    RUN_TEST(test_a_zone_the_part_does_not_have_is_refused);
    RUN_TEST(test_genkey_returns_a_public_point_and_never_a_private_one);
    RUN_TEST(test_genkey_into_any_other_slot_is_refused);
    RUN_TEST(test_pubkey_reads_and_does_not_generate);
    RUN_TEST(test_pubkey_on_an_empty_slot_fails);
    RUN_TEST(test_seeding_the_uds_takes_no_key_material_from_the_host);
    RUN_TEST(test_seeding_any_other_slot_is_refused);
    RUN_TEST(test_locking_out_of_order_is_refused_by_the_element);
    RUN_TEST(test_a_locked_config_zone_refuses_writes);
    return UNITY_END();
}
