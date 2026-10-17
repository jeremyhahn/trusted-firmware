/*
 * OTA_INFO on a no-boot build (#130, owner ruling m496): the OTA link with
 * handoff_none.c and platform_id_none.c where handoff.c and platform_id.c would
 * be. With no tfw-boot there is no platform block, and OTA_INFO reads it before
 * anything else (src/core/ota_ctl.c:73, load): the magic check at
 * boot/boot_platform.c:65-66 answers NOT_PROVISIONED, not the BAD_STATE of owner
 * ruling m496, and no reply bytes are written. Pinned as it is; not changed here.
 */
#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void) { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run_cmd(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static void test_ota_info_without_a_handoff_answers_not_provisioned(void)
{
    const uint8_t slot = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run_cmd(TFW_CMD_OTA_INFO, &slot, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ota_info_without_a_handoff_answers_not_provisioned);
    return UNITY_END();
}
