/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The eFuse-UDS provisioning backend (src/caps/provision_uds.c), driven
 * through the wire handlers of src/caps/provision.c over the part of
 * test/mock/prov_uds_mock.c: PROV_GENKEY burns a UDS drawn by the part and
 * returns the DeviceID point of exactly that UDS, refuses a second time, and
 * no reply of any command carries a byte run of the UDS.
 * docs/multi-arch/esp32-p4-design.md section 2.4 and section 3 row 6.
 */

#include <string.h>
#include "unity.h"
#include "provision.h"
#include "prov_uds_mock.h"
#include "tfw/protocol.h"
#include "crypto/sha256.h"
#include "crypto/deviceid_p256.h"

static uint8_t      inbuf[64];
static uint8_t      outbuf[128];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_prov_uds_reset(0x00u); }
void tearDown(void) {}

static void prep(const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
}

static tfw_status_t genkey(uint8_t slot)
{
    prep(&slot, 1u);
    return tfw_prov_cmd_genkey(&r, &w);
}

static tfw_status_t pubkey(uint8_t slot)
{
    prep(&slot, 1u);
    return tfw_prov_cmd_pubkey(&r, &w);
}

/* The host's own derivation from the UDS the part holds: the production
 * image's, HMAC-SHA256 of the label under the UDS, then the P-256 point. */
static void host_point(const uint8_t uds[32], uint8_t pub[64])
{
    uint8_t seed[32];

    tfw_hmac_sha256(uds, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    (uint16_t)TFW_DEVICEID_LABEL_LEN, seed);
    tfw_deviceid_p256_public(seed, pub);
}

/* 1 when any 8-byte run of the UDS appears anywhere in the reply. */
static int reply_carries(const uint8_t uds[32])
{
    uint8_t n = tfw_writer_len(&w);
    uint8_t i;
    uint8_t j;

    for (i = 0u; i + 8u <= 32u; i++) {
        for (j = 0u; (uint8_t)(j + 8u) <= n; j++) {
            if (memcmp(&outbuf[j], &uds[i], 8u) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* The UDS of the QEMU harness and test_deviceid_p256.c, 00 01 .. 1F, and the
 * point it pins there (SEED_PUB). A literal, so the derivation cannot drift
 * with the code it checks. */
static const uint8_t UDS_00_1F_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};

static void test_genkey_returns_the_pinned_point_of_the_uds_it_burned(void)
{
    static const uint8_t uds[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F
    };

    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    TEST_ASSERT_NOT_NULL(tfw_mock_prov_uds_burned());
    TEST_ASSERT_EQUAL_MEMORY(uds, tfw_mock_prov_uds_burned(), 32u);
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_MEMORY(UDS_00_1F_PUB, outbuf, 64u);
}

static void test_genkey_returns_the_host_derivation_from_the_burned_uds(void)
{
    uint8_t want[64];

    tfw_mock_prov_uds_reset(0xA5u);
    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_draws());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_burns());
    host_point(tfw_mock_prov_uds_burned(), want);
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_MEMORY(want, outbuf, 64u);
}

static void test_a_second_genkey_is_refused_before_anything_is_drawn(void)
{
    uint8_t first[32];

    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    memcpy(first, tfw_mock_prov_uds_burned(), sizeof first);

    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, genkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_draws());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_burns());
    TEST_ASSERT_EQUAL_MEMORY(first, tfw_mock_prov_uds_burned(), sizeof first);
}

static void test_pubkey_reads_the_point_back_and_burns_nothing(void)
{
    uint8_t point[64];

    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    memcpy(point, outbuf, sizeof point);

    TEST_ASSERT_EQUAL(TFW_OK, pubkey(0u));
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_MEMORY(point, outbuf, sizeof point);
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_burns());
}

static void test_pubkey_before_genkey_is_not_provisioned(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, pubkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_no_reply_carries_the_uds(void)
{
    uint8_t uds[32];
    uint8_t cfg[33] = { 2u };
    uint8_t lock[3] = { 0u, 0x12u, 0x34u };
    uint8_t slot    = 1u;

    tfw_mock_prov_uds_reset(0x3Cu);
    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    memcpy(uds, tfw_mock_prov_uds_burned(), sizeof uds);
    TEST_ASSERT_FALSE(reply_carries(uds));

    TEST_ASSERT_EQUAL(TFW_OK, pubkey(0u));
    TEST_ASSERT_FALSE(reply_carries(uds));

    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, genkey(0u));
    TEST_ASSERT_FALSE(reply_carries(uds));

    prep(0, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_prov_cmd_info(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    prep(cfg, (uint8_t)sizeof cfg);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_prov_cmd_write_config(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    prep(lock, (uint8_t)sizeof lock);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_prov_cmd_lock(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    prep(&slot, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_prov_cmd_seed_uds(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));

    /* The check itself sees a run when one is there. */
    prep(0, 0u);
    tfw_write_blob(&w, &uds[5], 8u);
    TEST_ASSERT_TRUE(reply_carries(uds));
}

static void test_a_failed_burn_is_a_status_and_the_part_stays_empty(void)
{
    tfw_mock_prov_uds_fail_burn(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, genkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_NULL(tfw_mock_prov_uds_burned());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, pubkey(0u));

    /* Nothing was burned, so the jig may try again. */
    tfw_mock_prov_uds_fail_burn(TFW_OK);
    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    TEST_ASSERT_EQUAL_MEMORY(UDS_00_1F_PUB, outbuf, 64u);
}

static void test_no_free_key_block_is_its_own_status(void)
{
    tfw_mock_prov_uds_fail_burn(TFW_ERR_NO_SLOT);
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, genkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_NULL(tfw_mock_prov_uds_burned());
}

static void test_a_draw_that_cannot_start_the_noise_source_burns_nothing(void)
{
    tfw_mock_prov_uds_fail_random(TFW_ERR_BUSY);
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, genkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_prov_uds_burns());
    TEST_ASSERT_NULL(tfw_mock_prov_uds_burned());
}

static void test_a_refused_hmac_after_the_burn_is_a_status_and_pubkey_recovers(void)
{
    tfw_mock_prov_uds_fail_hmac(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, genkey(0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_NOT_NULL(tfw_mock_prov_uds_burned());

    tfw_mock_prov_uds_fail_hmac(TFW_OK);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, genkey(0u));
    TEST_ASSERT_EQUAL(TFW_OK, pubkey(0u));
    TEST_ASSERT_EQUAL_MEMORY(UDS_00_1F_PUB, outbuf, 64u);
}

static void test_any_other_slot_is_refused_and_draws_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, genkey(1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, genkey(0xFFu));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_prov_uds_draws());
    TEST_ASSERT_EQUAL(TFW_OK, genkey(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, pubkey(1u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_genkey_returns_the_pinned_point_of_the_uds_it_burned);
    RUN_TEST(test_genkey_returns_the_host_derivation_from_the_burned_uds);
    RUN_TEST(test_a_second_genkey_is_refused_before_anything_is_drawn);
    RUN_TEST(test_pubkey_reads_the_point_back_and_burns_nothing);
    RUN_TEST(test_pubkey_before_genkey_is_not_provisioned);
    RUN_TEST(test_no_reply_carries_the_uds);
    RUN_TEST(test_a_failed_burn_is_a_status_and_the_part_stays_empty);
    RUN_TEST(test_no_free_key_block_is_its_own_status);
    RUN_TEST(test_a_draw_that_cannot_start_the_noise_source_burns_nothing);
    RUN_TEST(test_a_refused_hmac_after_the_burn_is_a_status_and_pubkey_recovers);
    RUN_TEST(test_any_other_slot_is_refused_and_draws_nothing);
    return UNITY_END();
}
