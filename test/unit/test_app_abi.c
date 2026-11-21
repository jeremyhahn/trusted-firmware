/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The descriptor's ABI check (src/core/app_runner.c:41) from the older side:
 * docs/application-sdk/engineering.md:218 refuses an `abi` that is not this
 * firmware's TFW_APP_ABI, so an application built against an older interface,
 * whose hooks have the old signatures, is refused before tfw_app_init as
 * surely as a newer one (test/unit/test_app.c). The current ABI is accepted.
 */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "app_runner.h"
#include "app_mock.h"
#include "hal_mock.h"
#include "tfw/app.h"
#include "tfw/protocol.h"
#include "vectors.h"

_Static_assert(TFW_APP_ABI >= 1u, "an ABI older than TFW_APP_ABI exists");

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_mock_app_reset();
}

void tearDown(void) {}

/* Neither init nor run is called: the log stays empty. */
static void test_an_older_abi_is_refused(void)
{
    tfw_mock_app_descriptor()->abi = (uint8_t)(TFW_APP_ABI - 1u);
    tfw_app_runner_start();
    tfw_app_runner_poll();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_UNSUPPORTED, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("", tfw_mock_app_log());
}

static void test_the_current_abi_is_accepted(void)
{
    tfw_mock_app_descriptor()->abi = (uint8_t)TFW_APP_ABI;
    tfw_app_runner_start();
    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_RUNNING, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_STRING("i", tfw_mock_app_log());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_older_abi_is_refused);
    RUN_TEST(test_the_current_abi_is_accepted);
    return UNITY_END();
}
