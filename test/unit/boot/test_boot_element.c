/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The element-backed bootloader backends, against the software element.
 *
 * These two backends exist for one reason: a 4 KB AVR boot section cannot hold
 * a software SHA-256 and a boot decision together. Moving the hash and the
 * signature check into the element takes all cryptography out of the boot
 * section -- 2 368 bytes of 4 096 on an ATmega328P, against 5 688 for the
 * smallest software-hash core with no verifier at all.
 *
 * THE ASSERTION THAT MATTERS is that the element's digest and the software
 * digest are the same bytes. If they ever differ, every golden measurement a
 * platform holds becomes wrong for half the fleet, silently, with no symptom
 * but devices that stop verifying.
 */

#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_verify.h"
#include "boot_hal.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "sha256.h"

void setUp(void)    { tfw_model_reset(); tfw_boot_mock_reset(); }
void tearDown(void) {}

/* ------------------------------------------------- the digest must agree */

/*
 * FIPS 180-4's own vector, so neither implementation is the other's oracle.
 * SHA-256("abc") is published; a test comparing the element to our software
 * hash alone would pass if both were wrong in the same way.
 */
static const uint8_t SHA_ABC[32] = {
    0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,
    0xB0,0x03,0x61,0xA3,0x96,0x17,0x7A,0x9C,0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD
};

static void test_the_element_computes_the_published_digest(void)
{
    uint8_t out[32];

    tfw_boot_hash((const uint8_t *)"abc", 3u, out);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SHA_ABC, out, 32);
}

/*
 * A measurement longer than one block, so the Start/Update/End sequence and the
 * tail are all exercised. 200 bytes is three whole blocks and an 8-byte
 * remainder -- the shape that breaks a driver which pads its own tail or sends
 * a short Update.
 */
static void test_a_measurement_matches_the_software_hash(void)
{
    uint8_t image[200];
    uint8_t want[32];
    uint8_t got[32];
    uint16_t i;

    for (i = 0u; i < sizeof image; i++) { image[i] = (uint8_t)(i * 7u + 3u); }
    memcpy(tfw_boot_mock_flash(), image, sizeof image);
    tfw_sha256(image, (uint16_t)sizeof image, want);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure((uint32_t)sizeof image, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/* An image that is an exact multiple of the block size has no tail, which is
 * the off-by-one the loop above is most likely to get wrong in the other
 * direction. */
static void test_a_measurement_of_whole_blocks_has_no_tail(void)
{
    uint8_t image[128];
    uint8_t want[32];
    uint8_t got[32];
    uint16_t i;

    for (i = 0u; i < sizeof image; i++) { image[i] = (uint8_t)i; }
    memcpy(tfw_boot_mock_flash(), image, sizeof image);
    tfw_sha256(image, (uint16_t)sizeof image, want);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_measure((uint32_t)sizeof image, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/*
 * No element, no measurement -- and specifically NOT a zero digest.
 *
 * A bootloader handed 32 zeros would compare them against a golden measurement,
 * fail, and refuse to boot, which happens to be safe. But a caller that
 * received TFW_OK with a digest of nothing has been told a measurement was taken
 * when none was, and somewhere downstream that becomes a device attesting to a
 * hash it never computed.
 */
static void test_an_absent_element_refuses_to_measure(void)
{
    uint8_t got[32];

    tfw_model_set_absent(1u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure(64u, got));
}

static void test_a_measurement_of_nothing_is_refused(void)
{
    uint8_t got[32];
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_measure(0u, got));
}

/* ------------------------------------------------------------- verify */

static void test_a_valid_signature_is_accepted(void)
{
    uint8_t sig[64];
    uint8_t i;

    for (i = 0u; i < 64u; i++) { sig[i] = (uint8_t)(0x40u + i); }
    tfw_model_set_verify_result(1u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_verify((const uint8_t *)"image", 5u, sig, 0));
    TEST_ASSERT_TRUE(tfw_model_verify_seen());
    /* The driver forwarded the signature it was given, byte for byte. A backend
     * that reconstructed or reordered it would verify something else. */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, tfw_model_verify_last_sig(), 64);
}

static void test_a_bad_signature_is_refused(void)
{
    uint8_t sig[64] = { 0 };

    tfw_model_set_verify_result(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify((const uint8_t *)"image", 5u, sig, 0));
}

/*
 * AN ABSENT ELEMENT IS A REFUSAL, NOT A PASS.
 *
 * This is the test that would matter on a bench. A bootloader that booted
 * anyway when its verifier did not answer is one an attacker defeats by cutting
 * two traces -- and it would look identical to a working device right up until
 * someone tried.
 */
static void test_an_absent_element_refuses_to_verify(void)
{
    uint8_t sig[64] = { 0 };

    tfw_model_set_verify_result(1u);   /* it WOULD pass, if it were there */
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_verify((const uint8_t *)"image", 5u, sig, 0));
}

/*
 * The key is the element's, named by a slot. A caller that supplies one is
 * using this backend wrongly, and honouring it would mean verifying against a
 * key an attacker who reached the flash could have put there -- which is the
 * entire thing stored mode exists to prevent.
 */
static void test_a_caller_supplied_key_is_refused(void)
{
    uint8_t sig[64] = { 0 };
    uint8_t pub[64] = { 0 };

    tfw_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_verify((const uint8_t *)"i", 1u, sig, pub));
}

/* And there is no way to read the key out, because the bootloader never has
 * it. A backend that handed one back would invite a caller to verify in
 * software, which is the arrangement this one exists to avoid. */
static void test_the_public_key_cannot_be_read_out(void)
{
    uint8_t pub[64];
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_pubkey(pub));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_element_computes_the_published_digest);
    RUN_TEST(test_a_measurement_matches_the_software_hash);
    RUN_TEST(test_a_measurement_of_whole_blocks_has_no_tail);
    RUN_TEST(test_an_absent_element_refuses_to_measure);
    RUN_TEST(test_a_measurement_of_nothing_is_refused);
    RUN_TEST(test_a_valid_signature_is_accepted);
    RUN_TEST(test_a_bad_signature_is_refused);
    RUN_TEST(test_an_absent_element_refuses_to_verify);
    RUN_TEST(test_a_caller_supplied_key_is_refused);
    RUN_TEST(test_the_public_key_cannot_be_read_out);
    return UNITY_END();
}
