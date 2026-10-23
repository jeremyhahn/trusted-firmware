/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The key slot that ships, boot/verify/p256_key_slot.c, against seeded mutants
 * (#163). Every one of its 64 bytes is erased flash, so the verify behind it
 * refuses a signature that is valid under a key made fresh in the test, and
 * refuses that key when it is supplied.
 *
 * Mutants tried, each killed by the test named:
 *   one slot byte 0xFF -> 0xFE (first, middle, last)  -> every_slot_byte_is_erased
 *   slot byte 0 0xFF -> 0x00                          -> every_slot_byte_is_erased
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "boot_verify.h"
#include "sha256.h"
#include "tfw/progmem.h"
#include "uECC.h"

extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

static uint32_t rng_state;

static int test_rng(uint8_t *dest, unsigned size)
{
    while (size--) {
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 17;
        rng_state ^= rng_state << 5;
        *dest++ = (uint8_t)rng_state;
    }
    return 1;
}

void setUp(void)
{
    rng_state = 0x51077EEDu;
    uECC_set_rng(test_rng);
}

void tearDown(void) {}

static void test_every_slot_byte_is_erased(void)
{
    for (uint8_t i = 0u; i < 64u; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFFu, tfw_boot_p256_key[i], "slot byte");
    }
}

static void test_a_valid_signature_is_refused(void)
{
    uint8_t priv[32], pub[64], digest[32], sig[64];
    const uint8_t msg[] = "an image the erased slot must refuse";

    TEST_ASSERT_EQUAL_INT(1, uECC_make_key(pub, priv, uECC_secp256r1()));
    tfw_sha256(msg, sizeof msg, digest);
    TEST_ASSERT_EQUAL_INT(1, uECC_sign(priv, digest, 32u, sig, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_INT(1, uECC_verify(pub, digest, 32u, sig, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, sizeof msg, sig, 0));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(msg, sizeof msg, sig, pub));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_slot_byte_is_erased);
    RUN_TEST(test_a_valid_signature_is_refused);
    return UNITY_END();
}
