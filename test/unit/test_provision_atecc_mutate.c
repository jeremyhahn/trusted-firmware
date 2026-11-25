/*
 * Mutation tests for src/caps/provision_atecc.c, each tfw_prov_* called
 * directly against the software element (test/model/atecc608a.c), with no
 * command layer between. The element is a model, never a part.
 *
 * The model's command log pins what reached the element -- opcode, both
 * parameters, the data -- so a fault in a zone, an address, a mode or a slot
 * is caught where it is made. The faults: a bound moved by one, a check
 * dropped or reordered, a status ignored or replaced, a parameter changed,
 * the sleep skipped, the wrong execution time.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "provision.h"

void setUp(void)    { tfw_model_reset(); }
void tearDown(void) { }

static void pattern(uint8_t *p, uint8_t seed)
{
    uint8_t i;

    for (i = 0u; i < 32u; i++) {
        p[i] = (uint8_t)(seed + i);
    }
}

static void check_cmd(uint8_t i, uint8_t op, uint8_t p1, uint16_t p2, uint8_t dlen)
{
    const tfw_model_cmd_t *c = tfw_model_log(i);

    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_HEX8(op, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(p1, c->p1);
    TEST_ASSERT_EQUAL_HEX16(p2, c->p2);
    TEST_ASSERT_EQUAL_UINT8(dlen, c->dlen);
}

/* ------------------------------------------------------------ tfw_prov_info */

/* Kills: any of the three commands' opcode, zone or address changed, the lock
 * bytes taken from a neighbour or swapped, the sleep skipped. */
static void test_info_reads_revision_block_0_and_the_lock_bytes_of_block_2(void)
{
    static const uint8_t want_rev[4] = { 0x00u, 0x00u, 0x60u, 0x05u };
    uint8_t blk2[32], rev[4], cfg[32], lock[2] = { 0xEEu, 0xEEu };

    /* A blank part (serial bytes in block 0, zeros in blocks 1 and 3) with a
     * pattern in block 2, so a read pointed at another block shows. */
    pattern(blk2, 0x80u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_write_config(2u, blk2));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_rev, rev, 4u);
    TEST_ASSERT_EQUAL_HEX8(0x01u, cfg[0]);
    TEST_ASSERT_EQUAL_HEX8(0x67u, cfg[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, cfg[31]);
    TEST_ASSERT_EQUAL_HEX8(blk2[22], lock[0]);
    TEST_ASSERT_EQUAL_HEX8(blk2[23], lock[1]);
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_model_log_count());
    check_cmd(1u, TFW_ATECC_OP_INFO, TFW_ATECC_INFO_REVISION, 0u, 0u);
    check_cmd(2u, TFW_ATECC_OP_READ, TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32, 0u, 0u);
    check_cmd(3u, TFW_ATECC_OP_READ, TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32, 16u, 0u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* Kills: the lock bytes read from block 0, LockValue and LockConfig swapped. */
static void test_info_tells_the_two_lock_bytes_apart(void)
{
    uint8_t rev[4], cfg[32], lock[2];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_CONFIG, 0x1234u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_HEX8(0x55u, lock[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, lock[1]);
}

/* Kills: the wake's status ignored or answered as anything but NACK. */
static void test_info_on_a_silent_bus_is_a_nack_and_sends_nothing(void)
{
    uint8_t rev[4], cfg[32], lock[2] = { 0xEEu, 0xEEu };

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[1]);
}

/* Kills: any of the three statuses ignored or flipped, the lock bytes copied
 * after a refused read, the sleep skipped on a failure. */
static void test_info_stops_at_the_first_refused_command(void)
{
    uint8_t n;

    for (n = 0u; n < 3u; n++) {
        uint8_t rev[4], cfg[32], lock[2] = { 0xEEu, 0xEEu };

        tfw_model_reset();
        tfw_model_fail_commands_after(n);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
        TEST_ASSERT_EQUAL_UINT8(n, tfw_model_log_count());
        TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[0]);
        TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[1]);
    }
    for (n = 0u; n < 3u; n++) {
        uint8_t rev[4], cfg[32], lock[2] = { 0xEEu, 0xEEu };

        tfw_model_reset();
        tfw_model_force_status(0x0Fu);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
        TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[0]);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
}

/* ---------------------------------------------------- tfw_prov_write_config */

/* Kills: the range bound moved by one either way, the protected bound moved,
 * the two checks reordered or dropped, a check after the wake. */
static void test_write_config_takes_blocks_2_and_3_only(void)
{
    uint8_t  data[32];
    unsigned b;

    pattern(data, 0x10u);
    tfw_model_set_absent(1u);
    for (b = 0u; b < 256u; b++) {
        tfw_status_t want = b >= 4u ? TFW_ERR_RANGE
                          : b < 2u  ? TFW_ERR_PROTECTED_REGION
                          :           TFW_ERR_NACK;

        TEST_ASSERT_EQUAL(want, tfw_prov_write_config((uint8_t)b, data));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* Kills: the zone, the address, the length or the data changed, the sleep
 * skipped, the element's answer replaced. */
static void test_write_config_writes_the_block_it_names(void)
{
    const uint8_t *slot0;
    uint8_t        data[32], cfg[32], rev[4], lock[2];
    uint8_t        b;

    for (b = 2u; b < 4u; b++) {
        tfw_model_reset();
        pattern(data, (uint8_t)(0x20u * b));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_write_config(b, data));
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
        check_cmd(0u, TFW_ATECC_OP_WRITE, TFW_ATECC_ZONE_CONFIG | TFW_ATECC_ZONE_32,
                  (uint16_t)(b * 8u), 32u);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(data, tfw_model_log(0u)->data, 32u);
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    }
    /* Block 2 read back where info reads it. */
    tfw_model_reset();
    pattern(data, 0xC0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_write_config(2u, data));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_HEX8(data[22], lock[0]);
    /* The data zone is untouched. */
    slot0 = tfw_model_slot(0u);
    TEST_ASSERT_NOT_NULL(slot0);
    TEST_ASSERT_EQUAL_HEX8(0u, slot0[0]);

    /* A locked config zone: the element's refusal comes back. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_CONFIG, 1u));
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_write_config(3u, data));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    tfw_model_reset();
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_write_config(2u, data));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* ------------------------------------------------------------ tfw_prov_lock */

/* Kills: either zone test dropped or widened, the CRC check dropped or moved
 * after the wake, the two checks reordered. */
static void test_lock_takes_the_two_zones_and_a_crc_only(void)
{
    unsigned z;

    tfw_model_set_absent(1u);
    for (z = 0u; z < 256u; z++) {
        tfw_status_t want = z > 1u ? TFW_ERR_RANGE : TFW_ERR_NACK;

        TEST_ASSERT_EQUAL(want, tfw_prov_lock((uint8_t)z, 1u));
        TEST_ASSERT_EQUAL(z > 1u ? TFW_ERR_RANGE : TFW_ERR_BAD_ARGC, tfw_prov_lock((uint8_t)z, 0u));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* Kills: the zone or the CRC changed on the way, the element's answer
 * replaced, the sleep skipped. */
static void test_lock_sends_the_zone_and_the_crc(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_CONFIG, 0xA55Au));
    check_cmd(0u, TFW_ATECC_OP_LOCK, TFW_ATECC_LOCK_CONFIG, 0xA55Au, 0u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    TEST_ASSERT_TRUE(tfw_model_config_locked());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_DATA, 0x0001u));
    check_cmd(1u, TFW_ATECC_OP_LOCK, TFW_ATECC_LOCK_DATA, 0x0001u, 0u);
    TEST_ASSERT_TRUE(tfw_model_data_locked());
    /* Twice is the element's refusal, and it comes back. */
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_DATA, 0x0001u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* ------------------------------------------------- genkey, pubkey, seed_uds */

/* Kills: the slot test dropped or pointed elsewhere, the wake's status
 * ignored, for each of the three. */
static void test_each_key_command_takes_its_one_slot(void)
{
    uint8_t  pub[64];
    unsigned s;

    tfw_model_set_absent(1u);
    for (s = 0u; s < 256u; s++) {
        TEST_ASSERT_EQUAL(s == TFW_ATECC_SLOT_DEVICEID ? TFW_ERR_NACK : TFW_ERR_RANGE,
                          tfw_prov_genkey((uint8_t)s, pub));
        TEST_ASSERT_EQUAL(s == TFW_ATECC_SLOT_DEVICEID ? TFW_ERR_NACK : TFW_ERR_RANGE,
                          tfw_prov_pubkey((uint8_t)s, pub));
        TEST_ASSERT_EQUAL(s == TFW_ATECC_SLOT_UDS ? TFW_ERR_NACK : TFW_ERR_RANGE,
                          tfw_prov_seed_uds((uint8_t)s));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* Kills: GenKey's mode swapped (private for public or back), its slot or
 * length changed, the answer replaced, the sleep skipped. */
static void test_genkey_makes_a_key_and_pubkey_only_reads_it(void)
{
    uint8_t first[64], again[64], second[64];

    /* An empty slot: Public mode is the element's refusal. */
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_pubkey(TFW_ATECC_SLOT_DEVICEID, first));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_genkey(TFW_ATECC_SLOT_DEVICEID, first));
    check_cmd(1u, TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PRIVATE, TFW_ATECC_SLOT_DEVICEID, 0u);
    TEST_ASSERT_NOT_NULL(tfw_model_deviceid_public());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tfw_model_deviceid_public(), first, 64u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_pubkey(TFW_ATECC_SLOT_DEVICEID, again));
    check_cmd(2u, TFW_ATECC_OP_GENKEY, TFW_ATECC_GENKEY_PUBLIC, TFW_ATECC_SLOT_DEVICEID, 0u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, again, 64u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    /* A second GenKey is a new key. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_genkey(TFW_ATECC_SLOT_DEVICEID, second));
    TEST_ASSERT_TRUE(memcmp(first, second, 64u) != 0);

    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_genkey(TFW_ATECC_SLOT_DEVICEID, second));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* Kills: Random, Nonce or Write's opcode, mode, zone, address or data
 * changed, a status ignored, the sleep skipped. What lands in the slot is
 * what the element's Random produced. */
static void test_seed_uds_sends_random_nonce_write(void)
{
    const uint8_t *slot;
    uint8_t        rnd[32], i;

    for (i = 0u; i < 32u; i++) {
        rnd[i] = (uint8_t)(0xA0u ^ i);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_model_log_count());
    check_cmd(0u, TFW_ATECC_OP_RANDOM, 0u, 0u, 0u);
    check_cmd(1u, TFW_ATECC_OP_NONCE, TFW_ATECC_NONCE_PASSTHRU, 0u, 32u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rnd, tfw_model_log(1u)->data, 32u);
    check_cmd(2u, TFW_ATECC_OP_WRITE, TFW_ATECC_ZONE_DATA | TFW_ATECC_ZONE_32,
              (uint16_t)(TFW_ATECC_SLOT_UDS << 3), 32u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rnd, tfw_model_log(2u)->data, 32u);
    slot = tfw_model_slot(TFW_ATECC_SLOT_UDS);
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rnd, slot, 32u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

static void test_seed_uds_stops_at_the_first_refused_command(void)
{
    static const uint8_t zero[32];
    uint8_t n;

    for (n = 0u; n < 3u; n++) {
        tfw_model_reset();
        tfw_model_fail_commands_after(n);
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
        TEST_ASSERT_EQUAL_UINT8(n, tfw_model_log_count());
        TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, tfw_model_slot(TFW_ATECC_SLOT_UDS), 32u);
    }
    /* A locked data zone: the Write's refusal is the answer. */
    tfw_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_CONFIG, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_DATA, 1u));
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
    TEST_ASSERT_EQUAL_UINT8(5u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

/* Kills: any command given a shorter wait than its datasheet maximum. */
/* Kills: a failed status ignored between two commands. One command outlives
 * its wait and fails alone; the model takes the commands after it, so a
 * function that carries on shows one more command in the log. */
static void test_a_command_that_times_out_ends_the_sequence(void)
{
    uint8_t rev[4], cfg[32], lock[2] = { 0xEEu, 0xEEu };

    tfw_model_reset();
    tfw_model_set_exec_ms(TFW_ATECC_OP_INFO, 1000u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[0]);

    tfw_model_reset();
    tfw_model_set_exec_ms(TFW_ATECC_OP_READ, 1000u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_HEX8(0xEEu, lock[0]);

    tfw_model_reset();
    tfw_model_set_exec_ms(TFW_ATECC_OP_RANDOM, 1000u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());

    tfw_model_reset();
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE, 1000u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_model_log_count());
}

static void test_every_command_waits_its_maximum_execution_time(void)
{
    uint8_t rev[4], cfg[32], lock[2], data[32], pub[64];

    tfw_model_set_exec_ms(TFW_ATECC_OP_INFO,   5u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_READ,   5u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_WRITE,  45u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_LOCK,   35u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_GENKEY, 115u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_RANDOM, 23u);
    tfw_model_set_exec_ms(TFW_ATECC_OP_NONCE,  20u);

    pattern(data, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_info(rev, cfg, lock));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_write_config(2u, data));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_genkey(TFW_ATECC_SLOT_DEVICEID, pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_pubkey(TFW_ATECC_SLOT_DEVICEID, pub));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_seed_uds(TFW_ATECC_SLOT_UDS));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_prov_lock(TFW_ATECC_LOCK_CONFIG, 1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_info_reads_revision_block_0_and_the_lock_bytes_of_block_2);
    RUN_TEST(test_info_tells_the_two_lock_bytes_apart);
    RUN_TEST(test_info_on_a_silent_bus_is_a_nack_and_sends_nothing);
    RUN_TEST(test_info_stops_at_the_first_refused_command);
    RUN_TEST(test_write_config_takes_blocks_2_and_3_only);
    RUN_TEST(test_write_config_writes_the_block_it_names);
    RUN_TEST(test_lock_takes_the_two_zones_and_a_crc_only);
    RUN_TEST(test_lock_sends_the_zone_and_the_crc);
    RUN_TEST(test_each_key_command_takes_its_one_slot);
    RUN_TEST(test_genkey_makes_a_key_and_pubkey_only_reads_it);
    RUN_TEST(test_seed_uds_sends_random_nonce_write);
    RUN_TEST(test_seed_uds_stops_at_the_first_refused_command);
    RUN_TEST(test_a_command_that_times_out_ends_the_sequence);
    RUN_TEST(test_every_command_waits_its_maximum_execution_time);
    return UNITY_END();
}
