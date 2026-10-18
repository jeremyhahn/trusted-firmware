/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/tier/cdi_atecc.c against the software ATECC608A (test/model/), which IS
 * the I2C HAL here, through the real driver src/crypto/atecc608a/atecc.c --
 * the arrangement test_boot_element uses.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_image.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "tfw/types.h"

void setUp(void)    { tfw_model_reset(); tfw_boot_mock_reset(); }
void tearDown(void) {}

/* The measurement 0x20..0x3F, and HMAC-SHA256 over it keyed by the model's
 * TEST UDS, SHA-256("tfwsim-atecc608a TEST UDS v1"), computed with Python's
 * hashlib and hmac -- not with the code under test. */
static const uint8_t kCdi[TFW_BOOT_MEASURE_LEN] = {
    0xae, 0x44, 0x0f, 0x23, 0xc0, 0xd4, 0x19, 0xdd, 0x86, 0x4c, 0x77, 0xd4, 0xa4, 0x91, 0xad, 0xff,
    0x93, 0x05, 0x2f, 0xca, 0x7a, 0x65, 0xae, 0x92, 0x88, 0xaf, 0x4d, 0x88, 0x71, 0x76, 0x24, 0x39,
};

static void measurement(uint8_t m[TFW_BOOT_MEASURE_LEN])
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i] = (uint8_t)(0x20u + i);
    }
}

/* A provisioned part on the bus, found by init. */
static void element_present(uint32_t counter)
{
    tfw_model_provision();
    tfw_model_set_counter(counter);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
}

static uint32_t counter_now(void)
{
    uint32_t v = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_read(&v));
    return v;
}

/* ------------------------------------------------------------ init, tier */

static void test_a_present_element_is_the_secure_element_tier(void)
{
    element_present(0u);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, tfw_boot_cdi_tier());
    TEST_ASSERT_FALSE(tfw_model_is_awake());      /* init puts it back to sleep */
}

/* No part on the bus still boots: init answers TFW_OK and the tier is NONE. */
static void test_an_absent_element_is_no_tier_and_still_boots(void)
{
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, tfw_boot_cdi_tier());
}

/* ----------------------------------------------------------------- derive */

static void test_a_provisioned_element_derives_the_hmac(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];

    element_present(0u);
    measurement(m);
    memset(cdi, 0, sizeof cdi);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);
    TEST_ASSERT_FALSE(tfw_model_is_awake());      /* sleep cleared TempKey */
}

static void test_no_element_derives_nothing(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_boot_cdi_derive(m, cdi));
}

/* Found at init, gone by the derive: the wake fails. */
static void test_an_element_lost_after_init_is_a_crypto_error(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    element_present(0u);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_cdi_derive(m, cdi));
}

/* A part with no UDS refuses HMAC_START with execution error 0x0F. The tier
 * returns the driver's status as it is (cdi_atecc.c:93, `return st`), and the
 * driver maps 0x0F to TFW_ERR_UNSUPPORTED. */
static void test_an_unprovisioned_element_refuses_the_hmac(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, tfw_boot_cdi_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

/* START taken, END dropped: the failure is reported and the part still slept. */
static void test_a_dropped_hmac_end_fails_the_derive(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN] = {0}, cdi[TFW_BOOT_MEASURE_LEN];

    element_present(0u);
    tfw_model_fail_commands_after(1u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

/* ------------------------------------------------------------- floor read */

static void test_the_floor_is_the_counter_little_endian(void)
{
    element_present(0x01020304u);
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, counter_now());
}

static void test_no_element_has_no_floor(void)
{
    uint32_t v = 9u;

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_write(10u));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
}

static void test_a_floor_read_that_cannot_wake_is_a_crypto_error(void)
{
    uint32_t v = 9u;

    element_present(4u);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
}

/* The part's 0x0F becomes TFW_ERR_CRYPTO here, never the driver's
 * TFW_ERR_UNSUPPORTED, which the caller would read as "no element". */
static void test_a_refused_counter_read_is_a_crypto_error(void)
{
    uint32_t v = 9u;

    element_present(4u);
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_UINT32(9u, v);
}

/* ------------------------------------------------------------ floor write */

static void test_a_floor_at_or_above_the_value_is_not_incremented(void)
{
    element_present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(5u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(2u));
    TEST_ASSERT_EQUAL_UINT32(5u, counter_now());
}

static void test_the_floor_is_raised_to_the_value(void)
{
    element_present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(8u));
    TEST_ASSERT_EQUAL_UINT32(8u, counter_now());
}

static void test_the_floor_is_raised_by_the_whole_permitted_step(void)
{
    element_present(5u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_write(5u + TFW_BOOT_FLOOR_MAX_STEP));
    TEST_ASSERT_EQUAL_UINT32(5u + TFW_BOOT_FLOOR_MAX_STEP, counter_now());
}

/* One past the step: the loop makes MAX_STEP + 1 increments, so the counter
 * does reach the value, but the loop ends before the read that would see it
 * and the write answers TFW_ERR_RANGE (cdi_atecc.c:145, :169). */
static void test_a_step_past_the_bound_is_a_range_error(void)
{
    element_present(5u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_floor_write(5u + TFW_BOOT_FLOOR_MAX_STEP + 1u));
    TEST_ASSERT_EQUAL_UINT32(5u + TFW_BOOT_FLOOR_MAX_STEP + 1u, counter_now());
}

static void test_a_floor_write_that_cannot_read_is_a_crypto_error(void)
{
    element_present(5u);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
}

/* The read's wake is answered, the increment's is not. */
static void test_a_lost_wake_before_the_increment_is_a_crypto_error(void)
{
    element_present(5u);
    tfw_model_fail_wakes_after(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
    tfw_model_fail_wakes_after(255u);             /* the part answers again */
    TEST_ASSERT_EQUAL_UINT32(5u, counter_now());
}

/* The read's command is taken, the increment's is NACKed. */
static void test_a_refused_increment_is_a_crypto_error(void)
{
    element_present(5u);
    tfw_model_fail_commands_after(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_write(6u));
    tfw_model_fail_commands_after(255u);          /* the part answers again */
    TEST_ASSERT_EQUAL_UINT32(5u, counter_now());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_present_element_is_the_secure_element_tier);
    RUN_TEST(test_an_absent_element_is_no_tier_and_still_boots);
    RUN_TEST(test_a_provisioned_element_derives_the_hmac);
    RUN_TEST(test_no_element_derives_nothing);
    RUN_TEST(test_an_element_lost_after_init_is_a_crypto_error);
    RUN_TEST(test_an_unprovisioned_element_refuses_the_hmac);
    RUN_TEST(test_a_dropped_hmac_end_fails_the_derive);
    RUN_TEST(test_the_floor_is_the_counter_little_endian);
    RUN_TEST(test_no_element_has_no_floor);
    RUN_TEST(test_a_floor_read_that_cannot_wake_is_a_crypto_error);
    RUN_TEST(test_a_refused_counter_read_is_a_crypto_error);
    RUN_TEST(test_a_floor_at_or_above_the_value_is_not_incremented);
    RUN_TEST(test_the_floor_is_raised_to_the_value);
    RUN_TEST(test_the_floor_is_raised_by_the_whole_permitted_step);
    RUN_TEST(test_a_step_past_the_bound_is_a_range_error);
    RUN_TEST(test_a_floor_write_that_cannot_read_is_a_crypto_error);
    RUN_TEST(test_a_lost_wake_before_the_increment_is_a_crypto_error);
    RUN_TEST(test_a_refused_increment_is_a_crypto_error);
    return UNITY_END();
}
