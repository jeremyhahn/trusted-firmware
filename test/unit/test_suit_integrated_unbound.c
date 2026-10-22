/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The integrated-payload reader on a board that binds no policy or script
 * class: built with the ATtiny85's profile, whose class ids are config.h's
 * all-zero defaults (docs/hsm-self-defence/adr-response-policy-format.md D2).
 * An all-zero id names no class, so nothing is read: not the 2560's policy set
 * or script, and not an envelope whose class is itself all zero, of either
 * kind, though each is validly signed under the caller's key.
 */
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "suit_vectors.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

#define BOTH  ((uint8_t)(TFW_SUIT_KIND_POLICY | TFW_SUIT_KIND_SCRIPT))
#define UNSET 0xEEu

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
}

void tearDown(void) {}

static void refused(const uint8_t *e, uint16_t n)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;
    uint8_t             kind    = UNSET;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                      tfw_suit_read_integrated(e, n, SUIT_TENANT_PUB, BOTH, &m, &payload, &kind));
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_EQUAL_HEX8(UNSET, kind);
}

static void test_another_boards_policy_set_is_refused(void)
{
    refused(SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
}

static void test_another_boards_script_is_refused(void)
{
    refused(SUIT_SCRIPT_ENVELOPE, sizeof SUIT_SCRIPT_ENVELOPE);
}

static void test_an_all_zero_policy_class_is_no_class(void)
{
    refused(SUIT_POLICY_ENVELOPE_CLASS_ZERO, sizeof SUIT_POLICY_ENVELOPE_CLASS_ZERO);
}

static void test_an_all_zero_script_class_is_no_class(void)
{
    refused(SUIT_SCRIPT_ENVELOPE_CLASS_ZERO, sizeof SUIT_SCRIPT_ENVELOPE_CLASS_ZERO);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_another_boards_policy_set_is_refused);
    RUN_TEST(test_another_boards_script_is_refused);
    RUN_TEST(test_an_all_zero_policy_class_is_no_class);
    RUN_TEST(test_an_all_zero_script_class_is_no_class);
    return UNITY_END();
}
