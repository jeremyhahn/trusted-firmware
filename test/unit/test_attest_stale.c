/*
 * ATTEST paging after a failed ATTEST.
 *
 * Evidence is live only after a complete build. An ATTEST that fails partway
 * -- here the signer refuses because the device lost its identity between two
 * challenges -- must leave no evidence behind, not the previous nonce's frame
 * and not its own half-built one (alg 0, zero signature). A page request after
 * it is refused, as docs/dice-attestation/api.md and tfw_attest_cmd say.
 */
#include <string.h>
#include "unity.h"
#include "attest.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "uds_mock.h"
#include "boot_soft_mock.h"
#include "handoff.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

static const uint8_t TEST_UDS[32] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
};

static const uint8_t TEST_MEAS[32] = { 0x4D, 0x45, 0x41, 0x53 };

static const uint8_t NONCE_A[TFW_ATTEST_NONCE_LEN] = { 0xA0, 0x01 };
static const uint8_t NONCE_B[TFW_ATTEST_NONCE_LEN] = { 0xB0, 0x02 };
static const uint8_t NONCE_C[TFW_ATTEST_NONCE_LEN] = { 0xC0, 0x03 };

/* The mock's unprovisioned state: no handoff, no UDS. The signer refuses
 * with TFW_ERR_NOT_PROVISIONED from inside build(). */
static void unprovision(void)
{
    tfw_mock_reset();
    tfw_boot_handoff_init();
    tfw_mock_set_uds(0);
    tfw_crypto_init();
}

static void provision(void)
{
    tfw_mock_boot_soft(TEST_UDS, TEST_MEAS);
    tfw_crypto_init();
}

void setUp(void)
{
    unprovision();
    tfw_attest_reset_state();
}

void tearDown(void) {}

static tfw_status_t run(const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_ATTEST, &r, &w);
}

static tfw_status_t page(uint8_t n)
{
    return run(&n, 1u);
}

/*
 * THE DEFECT. A good ATTEST(A), then an ATTEST(B) whose signing fails: the
 * page request must be refused, not answered OK with B's unsigned frame.
 */
static void test_a_failed_attest_after_a_good_one_refuses_paging(void)
{
    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE_A, TFW_ATTEST_NONCE_LEN));
    TEST_ASSERT_EQUAL(TFW_OK, page(1u));

    unprovision();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE_B, TFW_ATTEST_NONCE_LEN));

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, page(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, page(1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, page(3u));
}

/* A failed first ATTEST, with no success before it, leaves nothing to page. */
static void test_a_failed_first_attest_refuses_paging(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE_B, TFW_ATTEST_NONCE_LEN));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, page(0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, page(1u));
}

/* And the refusal is not sticky: a later good ATTEST(C) pages normally, with
 * C's nonce and a signed frame. */
static void test_a_good_attest_after_a_failed_one_pages_normally(void)
{
    uint8_t whole[TFW_ATTEST_LEN];
    uint8_t got;
    uint8_t n;

    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE_A, TFW_ATTEST_NONCE_LEN));
    unprovision();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE_B, TFW_ATTEST_NONCE_LEN));
    provision();

    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE_C, TFW_ATTEST_NONCE_LEN));
    memcpy(whole, &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
    got = (uint8_t)(tfw_writer_len(&w) - 2u);
    for (n = 1u; got < TFW_ATTEST_LEN; n++) {
        TEST_ASSERT_EQUAL(TFW_OK, page(n));
        TEST_ASSERT_EQUAL_UINT8(n,              outbuf[0]);
        TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, outbuf[1]);
        memcpy(&whole[got], &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
        got = (uint8_t)(got + (tfw_writer_len(&w) - 2u));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE_C, &whole[1], TFW_ATTEST_NONCE_LEN);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, whole[73]);   /* OFF_ALG */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_attest_after_a_good_one_refuses_paging);
    RUN_TEST(test_a_failed_first_attest_refuses_paging);
    RUN_TEST(test_a_good_attest_after_a_failed_one_pages_normally);
    return UNITY_END();
}
