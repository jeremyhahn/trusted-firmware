/*
 * A no-boot build on the wire (#130): the host library with handoff_none.c and
 * platform_id_none.c where handoff.c and platform_id.c would be, which is what a
 * board without tfw-boot links. ATTEST goes through the dispatcher as a host
 * sends it, and the evidence carries what those stand-ins answer.
 */
#include <string.h>
#include "unity.h"
#include "attest.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "uds_mock.h"

/* The evidence layout, as test_attest.c pins it. */
#define TEST_OFF_DEVICE_ID 17u
#define TEST_OFF_FW_HASH   33u

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

static const uint8_t TEST_UDS[32] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
};

static const uint8_t NONCE[TFW_ATTEST_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
};

void setUp(void)
{
    tfw_mock_reset();
    tfw_attest_reset_state();
    tfw_mock_set_uds(0);            /* unprovisioned by default */
    tfw_crypto_init();
}

static void provision(void)
{
    tfw_mock_set_uds(TEST_UDS);
    tfw_crypto_init();
}
void tearDown(void) {}

static tfw_status_t run_cmd(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t run(const uint8_t *p, uint8_t n)
{
    return run_cmd(TFW_CMD_ATTEST, p, n);
}

static void test_attest_on_a_no_boot_build_is_not_provisioned(void)
{
    /* handoff_none.c: no tfw-boot, so no DeviceID seed (#173). A UDS the
     * application can read is not used in its place: the seed is tfw-boot's
     * alone, so ATTEST answers NOT_PROVISIONED rather than sign. */
    provision();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE, TFW_ATTEST_NONCE_LEN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_attest_on_a_no_boot_build_is_not_provisioned);
    return UNITY_END();
}
