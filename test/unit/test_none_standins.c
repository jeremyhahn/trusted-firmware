/* The none stand-ins' no-op entry points: each is called and the stand-in's
 * observable answer is asserted unchanged after it. */

#include <stdint.h>
#include "unity.h"
#include "tfw/protocol.h"
#include "ota_trial.h"
#include "auth_session.h"
#include "update_request.h"

void setUp(void) {}
void tearDown(void) {}

/* ota_trial_none.c:9 */
static void test_ota_trial_start_returns(void)
{
    tfw_ota_trial_start();
    TEST_PASS();
}

/* ota_trial_none.c:10: no trial, so no runner to ask -- this link has none. */
static void test_ota_trial_after_init_returns(void)
{
    tfw_ota_trial_after_init();
    TEST_PASS();
}

/* ota_trial_none.c:11 */
static void test_ota_trial_poll_is_safe_to_call_twice(void)
{
    tfw_ota_trial_poll();
    tfw_ota_trial_poll();
    TEST_PASS();
}

/* auth_session_none.c:21 */
static void test_auth_session_init_leaves_gate_ok(void)
{
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_auth_gate());
}

/* update_request_none.c:17 */
static void test_update_request_arm_leaves_unsupported(void)
{
    tfw_update_request_arm();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_update_request_supported());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_update_request_session());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ota_trial_start_returns);
    RUN_TEST(test_ota_trial_after_init_returns);
    RUN_TEST(test_ota_trial_poll_is_safe_to_call_twice);
    RUN_TEST(test_auth_session_init_leaves_gate_ok);
    RUN_TEST(test_update_request_arm_leaves_unsupported);
    return UNITY_END();
}
