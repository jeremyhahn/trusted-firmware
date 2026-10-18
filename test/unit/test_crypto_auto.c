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
#include "../../src/core/auth_session.h"
#include "../../boot/boot_measure.h"

tfw_status_t tfw_crypto_soft_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg_id);

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

/* The element's auth session, as mk/firmware.mk's AUTH_RENAME names it: these
 * stand in for auth_session.c so the suite sees which tier's gate
 * src/core/auth_session_auto.c takes. */
static unsigned     element_inits, element_gates;
static tfw_status_t element_gate_answer;
void tfw_auth_element_session_init(void) { element_inits++; }
tfw_status_t tfw_auth_element_gate(void)
{
    element_gates++;
    return element_gate_answer;
}

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
    element_inits       = 0u;
    element_gates       = 0u;
    element_gate_answer = TFW_OK;
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

/* A bus that will not start is no element: src/crypto/auto/provider.c's
 * first probe step fails and the soft root runs. */
void test_a_bus_that_will_not_start_runs_the_soft_root(void)
{
    tfw_model_provision();
    tfw_model_fail_i2c_init(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    assert_soft_identity();
}

/* Every remaining entry of the dispatcher answers as the chosen provider's
 * own entry does, called directly. */
typedef struct {
    tfw_status_t (*random)(uint8_t *out, uint8_t len);
    tfw_status_t (*sha256)(const uint8_t *data, uint16_t len, uint8_t out[32]);
    tfw_status_t (*hmac)(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
    tfw_status_t (*counter)(uint8_t idx, uint32_t *out);
    tfw_status_t (*verify_operator)(const uint8_t digest[32], const uint8_t sig[64],
                                    const uint8_t pubkey[64]);
    tfw_status_t (*verify_image)(const uint8_t digest[32], const uint8_t sig[64]);
} provider_t;

tfw_status_t tfw_crypto_soft_random(uint8_t *out, uint8_t len);
tfw_status_t tfw_crypto_soft_sha256(const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t tfw_crypto_soft_counter(uint8_t idx, uint32_t *out);
tfw_status_t tfw_crypto_soft_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                             const uint8_t pubkey[64]);
tfw_status_t tfw_crypto_soft_verify_image(const uint8_t digest[32], const uint8_t sig[64]);
tfw_status_t tfw_crypto_element_random(uint8_t *out, uint8_t len);
tfw_status_t tfw_crypto_element_sha256(const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t tfw_crypto_element_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t tfw_crypto_element_counter(uint8_t idx, uint32_t *out);
tfw_status_t tfw_crypto_element_verify_operator(const uint8_t digest[32], const uint8_t sig[64],
                                                const uint8_t pubkey[64]);
tfw_status_t tfw_crypto_element_verify_image(const uint8_t digest[32], const uint8_t sig[64]);

static void assert_entries_follow(const provider_t *p)
{
    static const uint8_t msg[3] = { 'a', 'b', 'c' };
    uint8_t  got[64], want[64];
    uint32_t got_n = 0u, want_n = 0u;

    TEST_ASSERT_EQUAL(p->random(want, 32u), tfw_crypto_random(got, 32u));

    memset(got, 0, sizeof got);
    memset(want, 0xFF, sizeof want);
    TEST_ASSERT_EQUAL(p->sha256(msg, sizeof msg, want), tfw_crypto_sha256(msg, sizeof msg, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32u);

    memset(got, 0, sizeof got);
    memset(want, 0xFF, sizeof want);
    TEST_ASSERT_EQUAL(p->hmac(0u, msg, sizeof msg, want), tfw_crypto_hmac(0u, msg, sizeof msg, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32u);

    TEST_ASSERT_EQUAL(p->counter(0u, &want_n), tfw_crypto_counter(0u, &got_n));
    TEST_ASSERT_EQUAL_UINT32(want_n, got_n);

    memset(got, 0x11, sizeof got);
    TEST_ASSERT_EQUAL(p->verify_operator(digest, got, got), tfw_crypto_verify_operator(digest, got, got));
    TEST_ASSERT_EQUAL(p->verify_image(digest, got), tfw_crypto_verify_image(digest, got));
}

void test_a_soft_boot_answers_every_entry_from_the_soft_root(void)
{
    static const provider_t soft = {
        tfw_crypto_soft_random, tfw_crypto_soft_sha256, tfw_crypto_soft_hmac,
        tfw_crypto_soft_counter, tfw_crypto_soft_verify_operator, tfw_crypto_soft_verify_image
    };

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    assert_entries_follow(&soft);
}

void test_an_element_boot_answers_every_entry_from_the_element(void)
{
    static const provider_t element = {
        tfw_crypto_element_random, tfw_crypto_element_sha256, tfw_crypto_element_hmac,
        tfw_crypto_element_counter, tfw_crypto_element_verify_operator, tfw_crypto_element_verify_image
    };

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    assert_entries_follow(&element);
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

/* No handoff from tfw-boot: no boot chain is running, so no floor is in force
 * and the command is not valid in this state. */
void test_no_handoff_reports_no_floor(void)
{
    uint32_t floor = 0u;

    tfw_model_set_absent(1u);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_otactl_floor(&plat, &floor));
}

/* A boot on a tier this image has no floor for: the floor in force cannot be
 * named, so none is reported -- neither the EEPROM copy nor the counter. */
void test_a_boot_on_a_tier_with_no_floor_reports_none(void)
{
    uint32_t floor = 0xA5A5A5A5u;

    tfw_model_provision();
    boot_on((uint8_t)TFW_CRYPTO_NONE);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor(&plat, &floor));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, floor);
}

/* The auto gate (src/core/auth_session_auto.c): the element's on the element
 * tier, its answer passed through either way. */
void test_the_element_tier_gate_passes_what_the_element_gate_passes(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    element_gate_answer = TFW_OK;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT(1u, element_gates);
}

void test_the_element_tier_gate_refuses_what_the_element_gate_refuses(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    element_gate_answer = TFW_ERR_AUTH_REQUIRED;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT(1u, element_gates);
}

/* The soft tier's gate is auth_session_none.c's answer: TFW_OK, and the
 * element's gate never asked, even when it would refuse. */
void test_the_soft_tier_gate_answers_ok_without_the_element_gate(void)
{
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_NOT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    element_gate_answer = TFW_ERR_AUTH_REQUIRED;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT(0u, element_gates);
}

/* The session init is the element's on either tier. */
void test_the_session_init_is_the_element_s_on_the_element_tier(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_UINT(1u, element_inits);
}

void test_the_session_init_is_the_element_s_on_the_soft_tier(void)
{
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_NOT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    tfw_auth_session_init();
    TEST_ASSERT_EQUAL_UINT(1u, element_inits);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_element_boot_reports_the_element_counter);
    RUN_TEST(test_an_element_boot_whose_probe_failed_reports_the_element_counter);
    RUN_TEST(test_a_soft_boot_reports_the_eeprom_floor);
    RUN_TEST(test_an_element_boot_with_an_unreadable_counter_fails);
    RUN_TEST(test_a_boot_on_a_tier_with_no_floor_reports_none);
    RUN_TEST(test_no_handoff_reports_no_floor);
    RUN_TEST(test_no_element_runs_the_soft_root);
    RUN_TEST(test_an_element_that_answers_is_chosen);
    RUN_TEST(test_a_probe_that_fails_half_way_runs_the_soft_root_only);
    RUN_TEST(test_the_choice_never_changes_until_reset);
    RUN_TEST(test_the_element_tier_gate_passes_what_the_element_gate_passes);
    RUN_TEST(test_the_element_tier_gate_refuses_what_the_element_gate_refuses);
    RUN_TEST(test_the_soft_tier_gate_answers_ok_without_the_element_gate);
    RUN_TEST(test_the_session_init_is_the_element_s_on_the_element_tier);
    RUN_TEST(test_the_session_init_is_the_element_s_on_the_soft_tier);
    RUN_TEST(test_a_bus_that_will_not_start_runs_the_soft_root);
    RUN_TEST(test_a_soft_boot_answers_every_entry_from_the_soft_root);
    RUN_TEST(test_an_element_boot_answers_every_entry_from_the_element);
    return UNITY_END();
}
