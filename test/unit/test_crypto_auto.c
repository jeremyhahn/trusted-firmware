/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * CRYPTO=auto, src/crypto/auto/provider.c (issue #130 criterion 1): the
 * element when one answers, else the soft root, chosen once at init. Linked
 * against the software element, as test_atecc is, with both providers renamed
 * exactly as mk/firmware.mk renames them for the image.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "../mock/uds_mock.h"
#include "../../src/core/ota_floor.h"
#include "../../src/core/handoff.h"
#include "../../boot/boot_measure.h"

tfw_status_t tfw_crypto_soft_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id);

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static const uint8_t soft_uds[32] = {
    0x55, 0x44, 0x53, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
};
static const uint8_t digest[32] = { 0xd1, 0x9e, 0x57 };

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_mock_set_uds(soft_uds);
}

void tearDown(void) {}

/* The identity is the soft root's: the same signature its own provider makes. */
static void assert_soft_identity(void)
{
    uint8_t sig[64], want[64], alg = 0u, want_alg = 0u, pub[64];

    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_HMAC_SHA256, tfw_crypto_algorithms());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_HMAC_SHA256, alg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_soft_sign(digest, want, &want_alg));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, sig, 64u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_pubkey(pub));
}

void test_no_element_runs_the_soft_root(void)
{
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_soft_identity();
}

void test_an_element_that_answers_is_chosen(void)
{
    uint8_t sig[64], alg = 0u;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(digest, sig, &alg));
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
    TEST_ASSERT_TRUE(tfw_model_p256_verify(tfw_model_deviceid_public(), digest, sig));
}

/* Wakes, then fails its Info: the soft root, and it stays the soft root once
 * the element would answer again. */
void test_a_probe_that_fails_half_way_runs_the_soft_root_only(void)
{
    tfw_model_provision();
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_force_status(0x00u);
    assert_soft_identity();
}

/* Chosen once: an element that goes away after init fails its calls, it never
 * hands them to the soft key. */
void test_the_choice_never_changes_until_reset(void)
{
    uint8_t sig[64], alg = 0u;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_sign(digest, sig, &alg));
}

/* OTA_INFO's rollback floor follows the boot's tier, read from the handoff
 * (src/core/ota_floor_auto.c), never the application's own probe. */
static tfw_boot_platform_t plat;

static void boot_on(uint8_t tier)
{
    uint8_t cdi[32], measurement[32];

    memset(cdi, 0xC1, sizeof cdi);
    memset(measurement, 0x3E, sizeof measurement);
    tfw_boot_handoff_build(test_handoff, tier, cdi, measurement, 2u, 0u);
    tfw_boot_handoff_init();
    memset(&plat, 0, sizeof plat);
    plat.rollback_floor = 7u;
    (void)tfw_crypto_init();
}

static uint32_t element_counter(void)
{
    uint8_t v[4] = { 0u };

    (void)tfw_atecc_wake();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_atecc_exec(TFW_ATECC_OP_COUNTER, TFW_ATECC_COUNTER_READ,
                                             TFW_ATECC_COUNTER_BOOT, 0, 0u, v, 4u,
                                             TFW_ATECC_T_COUNTER));
    (void)tfw_atecc_sleep();
    return (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
}

void test_an_element_boot_reports_the_element_counter(void)
{
    uint32_t floor = 0u;

    tfw_model_provision();
    tfw_model_set_counter(0x00ABCDEFuL);
    boot_on((uint8_t)TFW_CRYPTO_SECURE_ELEMENT);
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(&plat, &floor));
    TEST_ASSERT_EQUAL_HEX32(element_counter(), floor);
}

/* The review's case: tfw-boot booted on the element, the application's probe
 * failed half-way and chose soft. The floor in force is still the counter. */
void test_an_element_boot_whose_probe_failed_reports_the_element_counter(void)
{
    uint32_t floor = 0u;

    tfw_model_provision();
    tfw_model_set_counter(0x00ABCDEFuL);
    tfw_model_force_status(0x0Fu);
    boot_on((uint8_t)TFW_CRYPTO_SECURE_ELEMENT);
    tfw_model_force_status(0x00u);
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(&plat, &floor));
    TEST_ASSERT_TRUE(floor != 7u);
    TEST_ASSERT_EQUAL_HEX32(element_counter(), floor);
}

/* A soft boot reads the platform block's floor, even with an element on the
 * bus that the application's probe chose. */
void test_a_soft_boot_reports_the_eeprom_floor(void)
{
    uint32_t floor = 0u;

    tfw_model_provision();
    tfw_model_set_counter(0x00ABCDEFuL);
    boot_on((uint8_t)TFW_CRYPTO_SOFTWARE);
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
}

/* An element boot whose counter cannot be read fails; it never answers from
 * the EEPROM copy. */
void test_an_element_boot_with_an_unreadable_counter_fails(void)
{
    uint32_t floor = 0u;

    tfw_model_provision();
    boot_on((uint8_t)TFW_CRYPTO_SECURE_ELEMENT);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor(&plat, &floor));
}

/* No handoff: the floor in force cannot be named, so none is reported. */
void test_no_handoff_reports_no_floor(void)
{
    uint32_t floor = 0u;

    tfw_model_set_absent(1u);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor(&plat, &floor));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_element_boot_reports_the_element_counter);
    RUN_TEST(test_an_element_boot_whose_probe_failed_reports_the_element_counter);
    RUN_TEST(test_a_soft_boot_reports_the_eeprom_floor);
    RUN_TEST(test_an_element_boot_with_an_unreadable_counter_fails);
    RUN_TEST(test_no_handoff_reports_no_floor);
    RUN_TEST(test_no_element_runs_the_soft_root);
    RUN_TEST(test_an_element_that_answers_is_chosen);
    RUN_TEST(test_a_probe_that_fails_half_way_runs_the_soft_root_only);
    RUN_TEST(test_the_choice_never_changes_until_reset);
    return UNITY_END();
}
