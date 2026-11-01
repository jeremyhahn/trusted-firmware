/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RESET on a 2560 image whose update path is the external store (#52).
 *
 * Linked against src/core/update_request_store.c -- what mk/firmware.mk links
 * where BOOT_AB and OTA_STAGE=1 -- instead of the real request module, so this
 * is the stage image's behaviour and not a simulation of it. Its tfw-boot
 * (BOOT_STORE=1) opens no session over the UART, so RESET(1) is refused with
 * the existing TFW_ERR_UNSUPPORTED, while the image's OTA slots still count.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "update_request.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/boot.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
}
void tearDown(void) {}

static tfw_status_t reset_with(uint8_t mode)
{
    inbuf[0] = mode;
    tfw_reader_init(&r, inbuf, 1u);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_RESET, &r, &w);
}

static uint8_t handoff_untouched(void)
{
    uint8_t i, untouched = 1u;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        if (tfw_mock_handoff()[i] != 0u) { untouched = 0u; }
    }
    return untouched;
}

/* The other path's command: a session this image's tfw-boot does not open. The
 * host is told so with the code it already knows, and the device stays up. */
static void test_reset_to_bootloader_is_unsupported_on_a_store_image(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, reset_with(1u));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_TRUE(handoff_untouched());
}

/* The board has a tfw-boot that serves an update path, the store; arming the
 * request it never reads writes nothing. */
static void test_a_store_image_serves_an_update_path_but_no_session(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_update_request_supported());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_update_request_session());
    tfw_update_request_arm();
    TEST_ASSERT_TRUE(handoff_untouched());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_to_bootloader_is_unsupported_on_a_store_image);
    RUN_TEST(test_a_store_image_serves_an_update_path_but_no_session);
    return UNITY_END();
}
