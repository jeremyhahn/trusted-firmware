/*
 * Mutation tests for boot/boot_cose.c (#163, #231): the OTA_ACTIVATE and
 * OTA_CONFIRM Sig_structures
 * (RFC 9052 §4.4, boot_verify.h) against literal bytes, at every boundary of
 * the version's shortest CBOR integer (RFC 8949 §4.2.1) -- so a bound off by
 * one, a wrong head byte, the version's bytes in the wrong order, the payload
 * length off by one or a field dropped each change a byte this file names.
 * The ES256 build's algorithm, as every P-256 backend sets it.
 */
#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_verify.h"

const uint8_t tfw_boot_cose_alg = 0x26u;   /* ES256, -7 */

/* ["Signature1", h'A10126', h'', bstr .cbor ... up to the payload's length. */
static const uint8_t HEAD[18] = {
    0x84, 0x6A, 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1',
    0x43, 0xA1, 0x01, 0x26, 0x40, 0x58,
};
/* ["tfw-ota-activate", bstr(32) ... */
static const uint8_t ACT[20] = {
    0x84, 0x70, 't', 'f', 'w', '-', 'o', 't', 'a', '-', 'a', 'c',
    't', 'i', 'v', 'a', 't', 'e', 0x58, 0x20,
};

static uint8_t hash[TFW_BOOT_MEASURE_LEN];

void setUp(void)
{
    uint8_t i;
    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        hash[i] = (uint8_t)(0xC0u + i);
    }
}

void tearDown(void) {}

/* Builds into a buffer of 0xEE and checks every byte, the length, and that
 * nothing past the length was written. */
static void check(uint32_t version, uint8_t mode, const uint8_t *tail, uint8_t tail_len)
{
    uint8_t b[TFW_BOOT_COSE_ACTIVATE_MAX + 8u];
    uint8_t n;
    uint8_t i;

    memset(b, 0xEE, sizeof b);
    n = tfw_boot_cose_activate(b, hash, version, mode);

    TEST_ASSERT_EQUAL_UINT8(18u + 1u + 20u + 32u + tail_len + 1u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HEAD, b, sizeof HEAD);
    TEST_ASSERT_EQUAL_UINT8(20u + 32u + tail_len + 1u, b[18]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ACT, &b[19], sizeof ACT);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hash, &b[39], TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tail, &b[71], tail_len);
    TEST_ASSERT_EQUAL_HEX8(mode, b[71u + tail_len]);
    for (i = n; i < sizeof b; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, b[i]);
    }
}

static void test_version_zero_is_one_byte(void)
{
    static const uint8_t t[] = { 0x00 };
    check(0u, 0u, t, sizeof t);
}

static void test_version_23_is_the_last_one_byte_integer(void)
{
    static const uint8_t t[] = { 0x17 };
    check(23u, 1u, t, sizeof t);
}

static void test_version_24_takes_a_one_byte_argument(void)
{
    static const uint8_t t[] = { 0x18, 0x18 };
    check(24u, 0u, t, sizeof t);
}

static void test_version_255_is_the_last_one_byte_argument(void)
{
    static const uint8_t t[] = { 0x18, 0xFF };
    check(255u, 1u, t, sizeof t);
}

static void test_version_256_takes_a_two_byte_argument_big_endian(void)
{
    static const uint8_t t[] = { 0x19, 0x01, 0x00 };
    check(256u, 0u, t, sizeof t);
}

static void test_version_65535_is_the_last_two_byte_argument(void)
{
    static const uint8_t t[] = { 0x19, 0xFF, 0xFF };
    check(65535u, 1u, t, sizeof t);
}

static void test_version_65536_takes_a_four_byte_argument_big_endian(void)
{
    static const uint8_t t[] = { 0x1A, 0x00, 0x01, 0x00, 0x00 };
    check(65536u, 0u, t, sizeof t);
}

static void test_the_largest_version_is_the_longest_structure(void)
{
    static const uint8_t t[] = { 0x1A, 0x12, 0x34, 0x56, 0x78 };
    check(0x12345678u, 1u, t, sizeof t);
    check(0xFFFFFFFFu, 1u, (const uint8_t[]){ 0x1A, 0xFF, 0xFF, 0xFF, 0xFF }, 5u);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_COSE_ACTIVATE_MAX, 18u + 1u + 20u + 32u + 5u + 1u);
}

/* ["tfw-ota-confirm", bstr(32) ... (#231): a three-element array, so a mode
 * byte appended, or the activate tag, changes a byte this file names. */
static const uint8_t CFM[19] = {
    0x83, 0x6F, 't', 'f', 'w', '-', 'o', 't', 'a', '-', 'c', 'o',
    'n', 'f', 'i', 'r', 'm', 0x58, 0x20,
};

static void check_confirm(uint32_t version, const uint8_t *tail, uint8_t tail_len)
{
    uint8_t b[TFW_BOOT_COSE_CONFIRM_MAX + 8u];
    uint8_t n;
    uint8_t i;

    memset(b, 0xEE, sizeof b);
    n = tfw_boot_cose_confirm(b, hash, version);

    TEST_ASSERT_EQUAL_UINT8(18u + 1u + 19u + 32u + tail_len, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HEAD, b, sizeof HEAD);
    TEST_ASSERT_EQUAL_UINT8(19u + 32u + tail_len, b[18]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CFM, &b[19], sizeof CFM);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hash, &b[38], TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tail, &b[70], tail_len);
    for (i = n; i < sizeof b; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xEEu, b[i]);
    }
}

static void test_confirm_version_zero_is_one_byte(void)
{
    check_confirm(0u, (const uint8_t[]){ 0x00 }, 1u);
    check_confirm(23u, (const uint8_t[]){ 0x17 }, 1u);
}

static void test_confirm_version_24_to_255_takes_a_one_byte_argument(void)
{
    check_confirm(24u, (const uint8_t[]){ 0x18, 0x18 }, 2u);
    check_confirm(255u, (const uint8_t[]){ 0x18, 0xFF }, 2u);
}

static void test_confirm_version_256_to_65535_takes_two_bytes_big_endian(void)
{
    check_confirm(256u, (const uint8_t[]){ 0x19, 0x01, 0x00 }, 3u);
    check_confirm(65535u, (const uint8_t[]){ 0x19, 0xFF, 0xFF }, 3u);
}

static void test_confirm_largest_version_is_the_longest_structure(void)
{
    check_confirm(65536u, (const uint8_t[]){ 0x1A, 0x00, 0x01, 0x00, 0x00 }, 5u);
    check_confirm(0x12345678u, (const uint8_t[]){ 0x1A, 0x12, 0x34, 0x56, 0x78 }, 5u);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_COSE_CONFIRM_MAX, 18u + 1u + 19u + 32u + 5u);
}

/* The domain separation (docs/ota/api.md): the same image's activate and
 * confirm structures differ, so neither signature serves for the other. */
static void test_confirm_and_activate_never_sign_the_same_bytes(void)
{
    uint8_t a[TFW_BOOT_COSE_ACTIVATE_MAX];
    uint8_t c[TFW_BOOT_COSE_CONFIRM_MAX];
    uint8_t na = tfw_boot_cose_activate(a, hash, 7u, 0u);
    uint8_t nc = tfw_boot_cose_confirm(c, hash, 7u);

    TEST_ASSERT_NOT_EQUAL(na, nc);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, c, nc));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_zero_is_one_byte);
    RUN_TEST(test_version_23_is_the_last_one_byte_integer);
    RUN_TEST(test_version_24_takes_a_one_byte_argument);
    RUN_TEST(test_version_255_is_the_last_one_byte_argument);
    RUN_TEST(test_version_256_takes_a_two_byte_argument_big_endian);
    RUN_TEST(test_version_65535_is_the_last_two_byte_argument);
    RUN_TEST(test_version_65536_takes_a_four_byte_argument_big_endian);
    RUN_TEST(test_the_largest_version_is_the_longest_structure);
    RUN_TEST(test_confirm_version_zero_is_one_byte);
    RUN_TEST(test_confirm_version_24_to_255_takes_a_one_byte_argument);
    RUN_TEST(test_confirm_version_256_to_65535_takes_two_bytes_big_endian);
    RUN_TEST(test_confirm_largest_version_is_the_longest_structure);
    RUN_TEST(test_confirm_and_activate_never_sign_the_same_bytes);
    return UNITY_END();
}
