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

/* An image that is an exact multiple of the block size: the last whole block
 * is the End's data, never an Update followed by an empty End -- the
 * off-by-one the loop above is most likely to get wrong in the other
 * direction. 128 bytes is Start, one Update, and an End carrying 64. */
static void test_a_measurement_of_whole_blocks_ends_with_a_full_block(void)
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
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_HEX8(0x01u, tfw_model_log(1u)->p1);
    TEST_ASSERT_EQUAL_HEX8(0x02u, tfw_model_log(2u)->p1);
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_model_log(2u)->dlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[64], tfw_model_log(2u)->data, 64);
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

/* tfw_boot_hash over more than one SHA block takes the Update loop. */
static void test_a_hash_over_two_blocks_matches_the_software_hash(void)
{
    uint8_t data[130], want[32], got[32];
    uint8_t i;

    for (i = 0; i < sizeof data; i++) {
        data[i] = (uint8_t)(i * 7u + 1u);
    }
    tfw_sha256(data, (uint16_t)sizeof data, want);
    tfw_boot_hash(data, (uint16_t)sizeof data, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* An element that refuses the hash leaves a zero digest, which matches no
 * key, and a refused measurement is an error, not a digest. */
static void test_an_element_refusal_fails_the_hash_and_the_measurement(void)
{
    uint8_t data[64] = {0}, zero[32] = {0}, got[32];

    tfw_model_force_status(0x0Fu);
    memset(got, 0xA5, sizeof got);
    tfw_boot_hash(data, (uint16_t)sizeof data, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, got, 32);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_measure(130u, got));
}

/* ------------------------------------- the commands tfw_boot_hash sends
 *
 * Issue #119: the offset was a uint8_t and wrapped from 192 to 0 past 256
 * bytes. The digests below are literals (hashlib over the same bytes), so the
 * code under test is never its own oracle, and the element's command log pins
 * every SHA it was sent: Start, one Update per whole block at its own offset,
 * then End with the tail (datasheet opcode 0x47, modes 0/1/2).
 */
static uint8_t g_hash_in[300];

static void fill_hash_in(void)
{
    uint16_t i;
    for (i = 0u; i < sizeof g_hash_in; i++) { g_hash_in[i] = (uint8_t)(i * 7u + 1u); }
}

static void assert_hash_commands(uint16_t len, uint8_t updates, uint8_t tail)
{
    const tfw_model_cmd_t *c;
    uint8_t u;

    TEST_ASSERT_EQUAL_UINT8(2u + updates, tfw_model_log_count());
    c = tfw_model_log(0u);
    TEST_ASSERT_EQUAL_HEX8(0x47u, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(0x00u, c->p1);
    TEST_ASSERT_EQUAL_UINT16(0u, c->p2);
    TEST_ASSERT_EQUAL_UINT8(0u, c->dlen);
    for (u = 0u; u < updates; u++) {
        c = tfw_model_log((uint8_t)(1u + u));
        TEST_ASSERT_EQUAL_HEX8(0x47u, c->opcode);
        TEST_ASSERT_EQUAL_HEX8(0x01u, c->p1);
        TEST_ASSERT_EQUAL_UINT16(64u, c->p2);
        TEST_ASSERT_EQUAL_UINT8(64u, c->dlen);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&g_hash_in[(uint16_t)u * 64u], c->data, 64);
    }
    c = tfw_model_log((uint8_t)(1u + updates));
    TEST_ASSERT_EQUAL_HEX8(0x47u, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(0x02u, c->p1);
    TEST_ASSERT_EQUAL_UINT16(tail, c->p2);
    TEST_ASSERT_EQUAL_UINT8(tail, c->dlen);
    TEST_ASSERT_EQUAL_UINT16(len, (uint16_t)updates * 64u + tail);
    if (tail) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&g_hash_in[(uint16_t)updates * 64u], c->data, tail);
    }
}

/* 300 bytes: Updates at 0, 64, 128 and 192, then End with the 44 bytes at 256.
 * The uint8_t offset went 192 -> 0 here and hashed the first block again. */
static void test_a_hash_past_256_bytes_does_not_wrap(void)
{
    static const uint8_t want[32] = {
        0xB1,0xB3,0x6F,0x51,0xF5,0x14,0xD1,0x1C,0x6C,0xD3,0xA9,0x65,0x63,0x27,0xF1,0x80,
        0x20,0x9D,0x50,0x39,0x61,0xFF,0x06,0x16,0xDB,0x46,0xCF,0x88,0x92,0x1D,0xEB,0x28
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 300u, got);
    assert_hash_commands(300u, 4u, 44u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* The public-key hash (boot_policy.c, 32 bytes): Start, then End with it all. */
static void test_the_32_byte_key_hash_is_start_then_end(void)
{
    static const uint8_t want[32] = {
        0x8C,0x73,0x1C,0x19,0x9B,0x13,0x0C,0xB0,0x96,0xCA,0xE9,0x12,0x9D,0xE0,0x68,0x0C,
        0x61,0xE6,0x06,0x06,0xED,0x5C,0x3B,0xA7,0x2E,0xBA,0xC5,0x8A,0xD6,0xAF,0x6D,0xF3
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 32u, got);
    assert_hash_commands(32u, 0u, 32u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* The verifier's message hash (verify_atecc608a.c, 48 bytes): the same shape. */
static void test_the_48_byte_message_hash_is_start_then_end(void)
{
    static const uint8_t want[32] = {
        0x9C,0xF4,0x0E,0xE1,0xD5,0x7C,0x7C,0xE9,0xCE,0xEB,0xCC,0x47,0x7B,0x54,0x8C,0x08,
        0x9F,0x2F,0x18,0x94,0x5F,0x68,0xC2,0xAC,0xAF,0x8B,0x6C,0x4A,0xD3,0x8B,0x04,0x12
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 48u, got);
    assert_hash_commands(48u, 0u, 48u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* Nothing at all: Start, then an End with no data -- SHA-256 of the empty string. */
static void test_a_hash_of_nothing_is_start_then_an_empty_end(void)
{
    static const uint8_t want[32] = {
        0xE3,0xB0,0xC4,0x42,0x98,0xFC,0x1C,0x14,0x9A,0xFB,0xF4,0xC8,0x99,0x6F,0xB9,0x24,
        0x27,0xAE,0x41,0xE4,0x64,0x9B,0x93,0x4C,0xA4,0x95,0x99,0x1B,0x78,0x52,0xB8,0x55
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 0u, got);
    assert_hash_commands(0u, 0u, 0u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* Exactly one block goes in the End, not an Update and an empty End. */
static void test_a_hash_of_one_block_is_all_tail(void)
{
    static const uint8_t want[32] = {
        0x66,0xBD,0x46,0x33,0xED,0x6F,0x71,0xC4,0xEC,0xFA,0x47,0x63,0xBF,0x7B,0xA1,0xC8,
        0xEC,0x76,0x12,0xDE,0x9A,0xA6,0xC0,0x57,0x8A,0x7B,0x67,0x52,0x07,0xC7,0x1E,0x0B
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 64u, got);
    assert_hash_commands(64u, 0u, 64u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
}

/* One byte past a block: one Update, then a one-byte End. */
static void test_a_hash_one_past_a_block_takes_one_update(void)
{
    static const uint8_t want[32] = {
        0x9F,0x7D,0xC4,0x71,0x07,0xB7,0x50,0xA1,0xF3,0xD3,0x5D,0xB5,0xD9,0x54,0x7F,0x24,
        0xEF,0x40,0xDA,0x5B,0x73,0x1B,0x95,0x40,0xD4,0xF4,0x37,0x10,0xA1,0x54,0xF6,0xC9
    };
    uint8_t got[32];

    fill_hash_in();
    tfw_boot_hash(g_hash_in, 65u, got);
    assert_hash_commands(65u, 1u, 1u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 32);
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
    RUN_TEST(test_a_measurement_of_whole_blocks_ends_with_a_full_block);
    RUN_TEST(test_an_absent_element_refuses_to_measure);
    RUN_TEST(test_a_measurement_of_nothing_is_refused);
    RUN_TEST(test_a_hash_over_two_blocks_matches_the_software_hash);
    RUN_TEST(test_an_element_refusal_fails_the_hash_and_the_measurement);
    RUN_TEST(test_a_hash_past_256_bytes_does_not_wrap);
    RUN_TEST(test_the_32_byte_key_hash_is_start_then_end);
    RUN_TEST(test_the_48_byte_message_hash_is_start_then_end);
    RUN_TEST(test_a_hash_of_nothing_is_start_then_an_empty_end);
    RUN_TEST(test_a_hash_of_one_block_is_all_tail);
    RUN_TEST(test_a_hash_one_past_a_block_takes_one_update);
    RUN_TEST(test_a_valid_signature_is_accepted);
    RUN_TEST(test_a_bad_signature_is_refused);
    RUN_TEST(test_an_absent_element_refuses_to_verify);
    RUN_TEST(test_a_caller_supplied_key_is_refused);
    RUN_TEST(test_the_public_key_cannot_be_read_out);
    return UNITY_END();
}
