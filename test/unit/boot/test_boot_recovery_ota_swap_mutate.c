/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/recovery/recovery_ota.c on a board with two slots (#163), built under
 * the 2560's profile and linked as test_boot_swap.c is: the mutant a board
 * with one slot cannot see, because there the swap calls do nothing
 * (boot_swap_none.c).
 */
#include <string.h>

#include "unity.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "boot_store.h"
#include "boot_swap.h"
#include "frame.h"

#define SLOT  ((uint32_t)TFW_SWAP_SLOT_LEN)
#define PRI   ((uint32_t)TFW_SWAP_PRIMARY_BASE)
#define SEC   ((uint32_t)TFW_SWAP_SECONDARY_BASE)
#define SENT  256u

static uint8_t  img_a[TFW_SWAP_SLOT_LEN];
static uint8_t  img_n[TFW_SWAP_SLOT_LEN];
static uint32_t refuse_at;
static unsigned evaluates;
static unsigned hand_overs;
static tfw_boot_platform_t plat;
static tfw_boot_platform_t stored;

void tfw_boot_platform_store(const tfw_boot_platform_t *p)
{
    stored = *p;
}

tfw_status_t tfw_boot_evaluate_at(uint32_t base, uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)floor;
    memset(out, 0, sizeof *out);
    evaluates++;
    return (base == refuse_at) ? TFW_ERR_CRYPTO : TFW_OK;
}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    return tfw_boot_evaluate_at(TFW_BOOT_APP_BASE, floor, out);
}

void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    (void)v;
    hand_overs++;
}

tfw_status_t tfw_boot_floor_load(const tfw_boot_platform_t *p, uint32_t *out)
{
    *out = p->rollback_floor;
    return TFW_OK;
}

void setUp(void)
{
    uint32_t i;

    tfw_boot_mock_reset();
    for (i = 0u; i < SLOT; i++) {
        img_a[i] = (uint8_t)((i * 7u) ^ (i >> 8));
        img_n[i] = (uint8_t)~img_a[i];
    }
    memcpy(&tfw_boot_mock_flash()[PRI], img_a, SLOT);
    memcpy(&tfw_boot_mock_flash()[SEC], img_n, SLOT);
    memset(img_n, 0xA5, SENT);   /* the sent image: SENT bytes over the rest */
    refuse_at  = 0xFFFFFFFFu;
    evaluates  = 0u;
    hand_overs = 0u;
    memset(&plat, 0, sizeof plat);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    plat.ota_state  = TFW_BOOT_OTA_IDLE;
    stored = plat;
}

void tearDown(void) {}

static void feed(uint8_t seq, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    uint8_t  buf[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(buf, sizeof buf, seq, cmd, p, n);

    TEST_ASSERT_TRUE(len > 0u);
    tfw_boot_mock_feed_rx(buf, len);
}

/* Kills dropping tfw_boot_swap_fall_back after a refused image
 * (recovery_ota.c:200): an image checked in the secondary and swapped in, then
 * refused by the gate in the primary, is swapped out again. The image before
 * it is back in the primary, the refused one in the secondary, no swap is
 * left half done, and nothing was handed over. */
static void test_an_image_refused_in_the_primary_is_swapped_back(void)
{
    uint8_t  b[4] = { (uint8_t)SENT, (uint8_t)(SENT >> 8), 0u, 0u };
    uint8_t  d[4u + 32u];
    uint32_t off;
    tfw_boot_session_limits_t lim = { 0u, 0u, 0u, 0u };

    feed(1u, TFW_CMD_OTA_BEGIN, b, 4u);
    memset(&d[4], 0xA5, 32u);
    for (off = 0u; off < SENT; off += 32u) {
        d[0] = (uint8_t)off; d[1] = (uint8_t)(off >> 8); d[2] = 0u; d[3] = 0u;
        feed(2u, TFW_CMD_OTA_DATA, d, (uint8_t)sizeof d);
    }
    feed(3u, TFW_CMD_OTA_END, 0, 0u);
    refuse_at = PRI;
    lim.idle_polls = 100u;
    tfw_boot_session_open(&plat, &lim);

    TEST_ASSERT_EQUAL_UINT(2u, evaluates);   /* the secondary, then the primary */
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_MEMORY(img_a, &tfw_boot_mock_flash()[PRI], SLOT);
    TEST_ASSERT_EQUAL_MEMORY(img_n, &tfw_boot_mock_flash()[SEC], SLOT);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_swap_in_progress());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored.ota_state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_image_refused_in_the_primary_is_swapped_back);
    return UNITY_END();
}
