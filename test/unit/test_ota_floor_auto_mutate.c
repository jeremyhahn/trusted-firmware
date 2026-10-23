/*
 * CRYPTO=auto's floor dispatch (src/core/ota_floor_auto.c, #163) alone: the
 * handoff's tier and both renamed floors are this suite's, so every tier byte
 * and the absent handoff can be put in front of the dispatch.
 *
 * The oracle is the dispatch's rule: the tier tfw-boot handed off picks the
 * floor, the element's for SECURE_ELEMENT and the EEPROM's for SOFTWARE, with
 * the caller's platform and floor passed through and the chosen floor's status
 * returned; no handoff is BAD_STATE and any other tier a crypto error, and
 * neither consults a floor.
 */
#include <stdint.h>
#include <stddef.h>

#include "unity.h"
#include "tfw/crypto.h"
#include "handoff.h"
#include "ota_floor.h"

static const uint8_t *tier_ptr;
static uint8_t        tier;
static unsigned       element_calls, soft_calls;
static const tfw_boot_platform_t *seen_p;
static uint32_t      *seen_out;
static tfw_status_t   floor_st;

const uint8_t *tfw_boot_handoff_tier(void)
{
    return tier_ptr;
}

tfw_status_t tfw_otactl_floor_element(const tfw_boot_platform_t *p, uint32_t *out)
{
    element_calls++;
    seen_p = p;
    seen_out = out;
    return floor_st;
}

tfw_status_t tfw_otactl_floor_soft(const tfw_boot_platform_t *p, uint32_t *out)
{
    soft_calls++;
    seen_p = p;
    seen_out = out;
    return floor_st;
}

static const tfw_boot_platform_t *platform;
static uint32_t floor_v;

void setUp(void)
{
    tier_ptr = &tier;
    tier = 0u;
    element_calls = soft_calls = 0u;
    seen_p = NULL;
    seen_out = NULL;
    floor_st = TFW_OK;
    platform = (const tfw_boot_platform_t *)(const void *)&tier;
}

void tearDown(void) {}

static void test_no_handoff_is_bad_state_and_consults_no_floor(void)
{
    tier_ptr = NULL;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_otactl_floor(platform, &floor_v));
    TEST_ASSERT_EQUAL_UINT(0u, element_calls + soft_calls);
}

static void test_every_tier_byte_picks_its_floor(void)
{
    for (uint16_t t = 0; t <= 0xffu; t++) {
        setUp();
        tier = (uint8_t)t;
        tfw_status_t st = tfw_otactl_floor(platform, &floor_v);
        if (t == (uint16_t)TFW_CRYPTO_SECURE_ELEMENT) {
            TEST_ASSERT_EQUAL(TFW_OK, st);
            TEST_ASSERT_EQUAL_UINT(1u, element_calls);
            TEST_ASSERT_EQUAL_UINT(0u, soft_calls);
        } else if (t == (uint16_t)TFW_CRYPTO_SOFTWARE) {
            TEST_ASSERT_EQUAL(TFW_OK, st);
            TEST_ASSERT_EQUAL_UINT(0u, element_calls);
            TEST_ASSERT_EQUAL_UINT(1u, soft_calls);
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, st);
            TEST_ASSERT_EQUAL_UINT(0u, element_calls + soft_calls);
        }
    }
}

static void test_the_platform_and_floor_are_passed_through(void)
{
    static const uint8_t tiers[] = { TFW_CRYPTO_SECURE_ELEMENT, TFW_CRYPTO_SOFTWARE };
    for (unsigned i = 0; i < sizeof tiers; i++) {
        setUp();
        tier = tiers[i];
        TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(platform, &floor_v));
        TEST_ASSERT_EQUAL_PTR(platform, seen_p);
        TEST_ASSERT_EQUAL_PTR(&floor_v, seen_out);
    }
}

static void test_the_chosen_floors_status_is_returned(void)
{
    static const uint8_t tiers[] = { TFW_CRYPTO_SECURE_ELEMENT, TFW_CRYPTO_SOFTWARE };
    static const tfw_status_t sts[] = { TFW_ERR_CRYPTO, TFW_ERR_BAD_STATE, TFW_ERR_INTERNAL };
    for (unsigned i = 0; i < sizeof tiers; i++) {
        for (unsigned s = 0; s < sizeof sts / sizeof sts[0]; s++) {
            setUp();
            tier = tiers[i];
            floor_st = sts[s];
            TEST_ASSERT_EQUAL(sts[s], tfw_otactl_floor(platform, &floor_v));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_handoff_is_bad_state_and_consults_no_floor);
    RUN_TEST(test_every_tier_byte_picks_its_floor);
    RUN_TEST(test_the_platform_and_floor_are_passed_through);
    RUN_TEST(test_the_chosen_floors_status_is_returned);
    return UNITY_END();
}
