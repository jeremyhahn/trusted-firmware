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
#include "tfw/protocol.h"
#include "tfw/config.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD + 8u];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_model_reset(); }
void tearDown(void) {}

static void prep(const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
}

/* --------------------------------------------------------------- INFO */

static void test_info_reports_the_element(void)
{
    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_info(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0x60u, outbuf[2]);        /* the 608 family byte */
    TEST_ASSERT_EQUAL_HEX8(0x05u, outbuf[3]);        /* 608C (AN5078, DS00005078A) */
}

/*
 * THE LOCK BYTES ARE IN THE REPLY, AND THEY ARE IN BLOCK 2.
 *
 * An earlier version of this command described reading block 2 in its comment
 * and then read only block 0, so the reply was 36 bytes and the host parser
 * indexed byte 90 of it. Every legal answer panicked the Go SDK. Nothing caught
 * it because neither side had a test for this command -- which is why the reply
 * length is asserted here rather than only the contents.
 */
static void test_info_carries_both_lock_bytes(void)
{
    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_info(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(38u, tfw_writer_len(&w));
    /* 0x55 is UNLOCKED, and the polarity is worth pinning: the value that looks
     * like a flag being set is the zone being open. */
    TEST_ASSERT_EQUAL_HEX8(0x55u, outbuf[36]);       /* LockValue:  data zone */
    TEST_ASSERT_EQUAL_HEX8(0x55u, outbuf[37]);       /* LockConfig: config zone */
}

static void test_info_reports_a_locked_part_as_locked(void)
{
    const uint8_t cfg[3] = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));

    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_info(&r, &w));
    TEST_ASSERT_NOT_EQUAL(0x55u, outbuf[37]);        /* the config zone closed */
}

/* A bus fault and an absent part look the same from here, so the status points
 * at the bus -- which is where the fault usually is. Saying "no element" would
 * send someone to the parts bin instead of to the wiring. */
static void test_a_silent_bus_is_reported_as_a_bus_fault(void)
{
    tfw_model_set_busy(255u);
    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_prov_cmd_info(&r, &w));
}

/* ------------------------------------------------------- WRITE_CONFIG */

static void test_a_config_block_is_written(void)
{
    uint8_t p[33];
    uint8_t i;

    p[0] = 2u;
    for (i = 0u; i < 32u; i++) { p[1u + i] = (uint8_t)i; }
    prep(p, 33u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_write_config(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_WRITE, tfw_model_last_opcode());
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
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_prov_cmd_write_config(&r, &w));
    p[0] = 1u;
    prep(p, 33u);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_prov_cmd_write_config(&r, &w));
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
    const uint8_t p[3] = { TFW_ATECC_LOCK_CONFIG, 0x00u, 0x00u };

    prep(p, 3u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, tfw_prov_cmd_lock(&r, &w));
    /* And nothing reached the element. A refusal that still issued the command
     * would be no refusal at all. */
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_model_last_opcode());
}

static void test_a_lock_with_a_crc_reaches_the_element(void)
{
    const uint8_t p[3] = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(p, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_LOCK, tfw_model_last_opcode());
}

/* -------------------------------------------------------------- KEYS */

static void test_genkey_returns_a_public_point_and_never_a_private_one(void)
{
    const uint8_t p[1] = { TFW_ATECC_SLOT_DEVICEID };

    tfw_model_provision();
    prep(p, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_genkey(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));   /* an uncompressed point */
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_GENKEY, tfw_model_last_opcode());
}

/* Generating into another slot would produce a key nothing in this firmware
 * will ever sign with -- a silent way to appear provisioned and not be. */
static void test_genkey_into_any_other_slot_is_refused(void)
{
    const uint8_t p[1] = { 5u };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_prov_cmd_genkey(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_model_last_opcode());
}

/* PUBKEY must not be able to replace the identity it is reading. The two use
 * different GenKey modes for exactly this reason, and `tfwprov --verify` depends
 * on it: a verify that could regenerate the key would destroy what it checked. */
static void test_pubkey_reads_and_does_not_generate(void)
{
    const uint8_t          p[1] = { TFW_ATECC_SLOT_DEVICEID };
    const tfw_model_cmd_t *last;

    tfw_model_provision();
    prep(p, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_pubkey(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
    /* the GenKey that ran was the public-key mode, never the generating one */
    TEST_ASSERT_TRUE(tfw_model_log_count() > 0u);
    last = tfw_model_log((uint8_t)(tfw_model_log_count() - 1u));
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_GENKEY, last->opcode);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_GENKEY_PUBLIC, last->p1);
}

static void test_pubkey_on_an_empty_slot_fails(void)
{
    const uint8_t p[1] = { TFW_ATECC_SLOT_DEVICEID };

    prep(p, 1u);                       /* not provisioned: no key in slot 0 */
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_cmd_pubkey(&r, &w));
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
    const uint8_t p[1] = { TFW_ATECC_SLOT_UDS };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_seed_uds(&r, &w));
    /* Random, then Nonce, then Write -- the last opcode proves it landed. */
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_WRITE, tfw_model_last_opcode());
    /* Nothing was written to the reply: there is no path by which a secret
     * leaves the device. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));

    /* And what landed is what the ELEMENT produced. The model's Random is a
     * known sequence, so asserting the slot holds it proves the value came from
     * inside the part rather than from anything the host supplied -- which is
     * the property the whole tier depends on and the one a passing call would
     * not otherwise demonstrate. */
    {
        const uint8_t *slot = tfw_model_slot(TFW_ATECC_SLOT_UDS);
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
    const uint8_t data_first[3] = { TFW_ATECC_LOCK_DATA, 0x34u, 0x12u };
    const uint8_t cfg[3]        = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(data_first, 3u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_FALSE(tfw_model_data_locked());

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_TRUE(tfw_model_config_locked());

    prep(data_first, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_TRUE(tfw_model_data_locked());

    /* And a second lock of the same zone is refused rather than silently
     * succeeding, which is what a host's retry would otherwise look like. */
    prep(cfg, 3u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
}

/* A locked config zone refuses further writes. If this ever passes, the
 * irreversibility this whole procedure is built around is not real. */
static void test_a_locked_config_zone_refuses_writes(void)
{
    const uint8_t cfg[3] = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };
    uint8_t       blk[33] = { 2u };

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    prep(blk, 33u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_cmd_write_config(&r, &w));
}

/* The key the PROV_WRITE_PUBKEY tests write: X = 0x10.., Y = 0x80.. */
static void test_key(uint8_t key[64])
{
    uint8_t i;

    for (i = 0u; i < 64u; i++) {
        key[i] = (uint8_t)((i < 32u ? 0x10u : 0x60u) + i);
    }
}

/* One PROV_WRITE_PUBKEY request: [slot][half][the half's coordinate]. */
static tfw_status_t write_half(uint8_t slot, uint8_t half, const uint8_t key[64], uint8_t n)
{
    uint8_t req[34];

    req[0] = slot;
    req[1] = half;
    memcpy(&req[2], &key[half * 32u], 32u);
    prep(req, n);
    return tfw_prov_cmd_write_pubkey(&r, &w);
}

static void lock_config(void)
{
    const uint8_t cfg[3] = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };

    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
}

static void test_a_public_key_is_written_and_read_back(void)
{
    static const uint8_t slots_ok[2] = { 2u, 15u };
    uint8_t              key[64];
    const uint8_t       *s;
    uint8_t              k;

    test_key(key);
    lock_config();
    for (k = 0u; k < 2u; k++) {
        /* Each reply is the slot as read back: after X, X and an empty Y. */
        TEST_ASSERT_EQUAL(TFW_OK, write_half(slots_ok[k], 0u, key, 34u));
        TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(key, outbuf, 32u);
        TEST_ASSERT_EACH_EQUAL_HEX8(0u, &outbuf[32], 32u);
        TEST_ASSERT_EQUAL(TFW_OK, write_half(slots_ok[k], 1u, key, 34u));
        TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(key, outbuf, 64u);
        /* The slot holds the stored form Verify reads: pad4|X|pad4|Y. */
        s = tfw_model_clear_slot(slots_ok[k]);
        TEST_ASSERT_EACH_EQUAL_HEX8(0u, s, 4u);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(key, &s[4], 32u);
        TEST_ASSERT_EACH_EQUAL_HEX8(0u, &s[36], 4u);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&key[32], &s[40], 32u);
    }
}

static void test_a_public_key_needs_the_config_zone_locked(void)
{
    uint8_t key[64];

    test_key(key);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, write_half(15u, 0u, key, 34u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_a_slot_locked_slot_is_refused(void)
{
    uint8_t blk[33] = { 2u };
    uint8_t key[64];

    /* SlotLocked is config bytes 88-89, block 2 offsets 24-25; clear slot
     * 15's bit, then lock the config. */
    blk[1u + 24u] = 0xFFu;
    blk[1u + 25u] = 0x7Fu;
    prep(blk, 33u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_write_config(&r, &w));
    lock_config();
    test_key(key);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, write_half(15u, 0u, key, 34u));
    TEST_ASSERT_EACH_EQUAL_HEX8(0u, tfw_model_clear_slot(15u), 72u);
}

static void test_a_public_key_of_the_wrong_length_is_refused(void)
{
    uint8_t key[64];

    test_key(key);
    lock_config();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, write_half(15u, 0u, key, 33u));
}

static void test_a_public_key_into_any_other_slot_or_half_is_refused(void)
{
    static const uint8_t bad[4] = { 0u, 3u, 14u, 16u };
    uint8_t              key[64];
    uint8_t              k;

    test_key(key);
    lock_config();
    for (k = 0u; k < 4u; k++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, write_half(bad[k], 0u, key, 34u));
    }
    key[0] = 15u;
    key[1] = 2u;
    prep(key, 34u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_prov_cmd_write_pubkey(&r, &w));
    TEST_ASSERT_EACH_EQUAL_HEX8(0u, tfw_model_clear_slot(15u), 72u);
}

static void test_a_public_key_write_reports_the_element_failing(void)
{
    uint8_t key[64];
    uint8_t after;

    test_key(key);
    /* After the config read (1), and after the nine writes (10): each failure
     * is returned, and no reply is written. */
    for (after = 1u; after <= 10u; after = (uint8_t)(after + 9u)) {
        tfw_model_reset();
        lock_config();
        tfw_model_fail_commands_after(after);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, write_half(15u, 0u, key, 34u));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    }
    tfw_model_reset();
    tfw_model_set_busy(255u);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, write_half(15u, 0u, key, 34u));
}

static void test_seeding_any_other_slot_is_refused(void)
{
    const uint8_t p[1] = { 3u };

    prep(p, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_prov_cmd_seed_uds(&r, &w));
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_model_last_opcode());
}

/*
 * A part that takes the whole of every maximum execution time (cryptoauthlib's
 * ATECC608 M0 table: Read 5 ms, Write 45, Lock 35, GenKey 115, Random 23,
 * Nonce 20) still completes the whole procedure. Before the 608C the firmware
 * waited 1, 26 and 15 ms for Read, Write and Lock, and timed out on each.
 */
static void test_the_procedure_survives_a_part_at_its_maximum_execution_times(void)
{
    const uint8_t slot[1] = { TFW_ATECC_SLOT_DEVICEID };
    const uint8_t uds[1]  = { TFW_ATECC_SLOT_UDS };
    const uint8_t cfg[3]  = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };
    const uint8_t dat[3]  = { TFW_ATECC_LOCK_DATA, 0x34u, 0x12u };
    uint8_t       blk[33] = { 2u };

    tfw_model_set_exec_ms(TFW_ATECC_OP_INFO,   5u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_READ,   5u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_WRITE,  45u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_LOCK,   35u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_GENKEY, 115u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_RANDOM, 23u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE,  20u);

    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_info(&r, &w));
    prep(blk, 33u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_write_config(&r, &w));
    prep(cfg, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    prep(slot, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_genkey(&r, &w));
    prep(slot, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_pubkey(&r, &w));
    prep(uds, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_seed_uds(&r, &w));
    prep(dat, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_TRUE(tfw_model_data_locked());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_procedure_survives_a_part_at_its_maximum_execution_times);
    RUN_TEST(test_info_reports_the_element);
    RUN_TEST(test_info_carries_both_lock_bytes);
    RUN_TEST(test_info_reports_a_locked_part_as_locked);
    RUN_TEST(test_a_silent_bus_is_reported_as_a_bus_fault);
    RUN_TEST(test_a_config_block_is_written);
    RUN_TEST(test_the_blocks_that_are_not_ours_are_refused);
    RUN_TEST(test_a_lock_without_a_summary_crc_is_refused);
    RUN_TEST(test_a_lock_with_a_crc_reaches_the_element);
    RUN_TEST(test_genkey_returns_a_public_point_and_never_a_private_one);
    RUN_TEST(test_genkey_into_any_other_slot_is_refused);
    RUN_TEST(test_pubkey_reads_and_does_not_generate);
    RUN_TEST(test_pubkey_on_an_empty_slot_fails);
    RUN_TEST(test_seeding_the_uds_takes_no_key_material_from_the_host);
    RUN_TEST(test_seeding_any_other_slot_is_refused);
    RUN_TEST(test_locking_out_of_order_is_refused_by_the_element);
    RUN_TEST(test_a_locked_config_zone_refuses_writes);
    RUN_TEST(test_a_public_key_is_written_and_read_back);
    RUN_TEST(test_a_public_key_needs_the_config_zone_locked);
    RUN_TEST(test_a_slot_locked_slot_is_refused);
    RUN_TEST(test_a_public_key_of_the_wrong_length_is_refused);
    RUN_TEST(test_a_public_key_into_any_other_slot_or_half_is_refused);
    RUN_TEST(test_a_public_key_write_reports_the_element_failing);
    return UNITY_END();
}
