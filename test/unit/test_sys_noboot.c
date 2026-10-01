/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * RESET on a board with no boot section.
 *
 * Linked against src/core/update_request_none.c -- what mk/avr.mk links when the
 * board profile defines no RAM_END -- instead of the real request module, so
 * this is the ATtiny85's behaviour and not a simulation of it.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "hal_mock.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "ag/boot.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

void setUp(void)
{
    ag_mock_reset();
    ag_sys_reset_state();
}
void tearDown(void) {}

static ag_status_t reset_with(uint8_t mode)
{
    inbuf[0] = mode;
    ag_reader_init(&r, inbuf, 1u);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_dispatch(AG_CMD_RESET, &r, &w);
}

/* Honest rather than quiet: a host that asked for a bootloader this part does
 * not have is told so, and the device does not reset out from under it. */
static void test_reset_to_bootloader_is_unsupported_without_a_boot_section(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, reset_with(1u));
    TEST_ASSERT_FALSE(ag_sys_reset_pending());
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, ag_mock_reset_count());
}

static void test_soft_reset_still_works_without_a_boot_section(void)
{
    uint8_t i, untouched = 1u;

    TEST_ASSERT_EQUAL(AG_OK, reset_with(0u));
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, ag_mock_reset_count());
    for (i = 0u; i < AG_BOOT_REQUEST_LEN; i++) {
        if (ag_mock_handoff()[i] != 0u) { untouched = 0u; }
    }
    TEST_ASSERT_TRUE(untouched);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_to_bootloader_is_unsupported_without_a_boot_section);
    RUN_TEST(test_soft_reset_still_works_without_a_boot_section);
    return UNITY_END();
}
