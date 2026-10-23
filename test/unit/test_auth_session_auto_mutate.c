/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/core/auth_session_auto.c alone (#130, #163): CRYPTO=auto's choice of
 * auth gate, against the three seams it calls, faked here -- the tier the
 * dispatch fixed, and auth_session.c's init and gate under their AUTH_RENAME
 * names. test_crypto_auto links the real ones; this pins the choice itself.
 *
 * The mutants this kills: the init forwarded never or twice, the tier
 * comparison inverted or moved to the soft tier, the element's answer
 * replaced by TFW_OK, the soft root's answer replaced by a refusal, the
 * element's gate asked when the soft root was chosen. The comparison moved to
 * >= is equivalent under auto: its provider reports the element's tier or the
 * soft root's, never TPM2.
 */
#include "unity.h"
#include "tfw/crypto.h"
#include "auth_session.h"

static tfw_crypto_tier_t tier;
static tfw_status_t      element_says;
static unsigned          element_inits;
static unsigned          element_gates;

tfw_crypto_tier_t tfw_crypto_tier(void)
{
    return tier;
}

void tfw_auth_element_session_init(void)
{
    element_inits++;
}

tfw_status_t tfw_auth_element_gate(void)
{
    element_gates++;
    return element_says;
}

void setUp(void)
{
    tier          = TFW_CRYPTO_SECURE_ELEMENT;
    element_says  = TFW_OK;
    element_inits = 0u;
    element_gates = 0u;
}

void tearDown(void) {}

static void test_init_starts_the_element_session_once_on_either_tier(void)
{
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_UINT(1u, element_inits);
    TEST_ASSERT_EQUAL_UINT(0u, element_gates);

    tier = TFW_CRYPTO_SOFTWARE;
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_UINT(2u, element_inits);
    TEST_ASSERT_EQUAL_UINT(0u, element_gates);
}

/* The element chosen: its gate's answer, whatever it is, and only that. */
static void test_the_element_tier_answers_with_the_element_gate(void)
{
    static const tfw_status_t says[] = { TFW_OK, TFW_ERR_AUTH_REQUIRED, TFW_ERR_CRYPTO };
    unsigned                  i;

    for (i = 0u; i < sizeof says / sizeof says[0]; i++) {
        element_says = says[i];
        TEST_ASSERT_EQUAL(says[i], tfw_auth_gate());
        TEST_ASSERT_EQUAL_UINT(i + 1u, element_gates);
    }
    TEST_ASSERT_EQUAL_UINT(0u, element_inits);
}

/* The soft root chosen: auth_session_none.c's answer, and the element's gate
 * is never asked -- it has no session to answer from. */
static void test_the_soft_root_is_permissive_and_never_asks_the_element(void)
{
    element_says = TFW_ERR_AUTH_REQUIRED;
    tier         = TFW_CRYPTO_SOFTWARE;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT(0u, element_gates);
}

/* Before the dispatch has found anything: not the element, so not its gate. */
static void test_no_provider_is_not_the_element(void)
{
    element_says = TFW_ERR_AUTH_REQUIRED;
    tier         = TFW_CRYPTO_NONE;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT(0u, element_gates);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_the_element_session_once_on_either_tier);
    RUN_TEST(test_the_element_tier_answers_with_the_element_gate);
    RUN_TEST(test_the_soft_root_is_permissive_and_never_asks_the_element);
    RUN_TEST(test_no_provider_is_not_the_element);
    return UNITY_END();
}
