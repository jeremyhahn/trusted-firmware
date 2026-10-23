/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/verify/verify_ed25519_donna.c with its key slot PROVISIONED (#163).
 *
 * test_boot_ed25519 links the slot as it ships, erased, so only the refusal
 * path of tfw_boot_pubkey ever ran and a mutant of its erased check -- an
 * |= for the &=, a loop that stops one byte short -- gave the same answer.
 * This link is the same source with the slot patched after compilation, as a
 * provisioning jig patches the image (mk/boot.mk, the cdi_soft_uds
 * arrangement): the slot holds RFC 8032 TEST 1's public key, so the key read
 * out is the one that verifies TEST 1's signature.
 */

#include <string.h>
#include "unity.h"
#include "boot_verify.h"

/* RFC 8032 section 7.1, TEST 1: the slot's patched contents (mk/boot.mk). */
static const uint8_t T1_PUB[32] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3,
    0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
    0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};
static const uint8_t T1_SIG[64] = {
    0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2, 0xcc,
    0x80, 0x6e, 0x82, 0x8a, 0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
    0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55, 0x5f, 0xb8, 0x82, 0x15,
    0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
    0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24, 0x65, 0x51, 0x41, 0x43,
    0x8e, 0x7a, 0x10, 0x0b,
};

void setUp(void)    {}
void tearDown(void) {}

/* Kills: |= for &= (an erased-looking accumulator), the erased comparison
 * flipped, and a TFW_OK return dropped. */
static void test_a_provisioned_slot_is_read_out(void)
{
    uint8_t pub[32];

    memset(pub, 0xA5, sizeof pub);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_pubkey(pub));
    /* Every byte, the last included: a loop that stops short leaves 0xA5. */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(T1_PUB, pub, 32);
}

/* The key read out of the slot is the one the backend verifies with: the
 * path tfw-boot takes, tfw_boot_pubkey then tfw_boot_verify. */
static void test_the_slot_key_verifies_rfc8032_test1(void)
{
    uint8_t pub[32], msg[1] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_pubkey(pub));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_verify(msg, 0u, T1_SIG, pub));
}

/* Kills: a msglen ignored by the backend. TEST 1 signs the empty message; the
 * same signature over one byte must not verify. */
static void test_the_slot_key_refuses_test1_over_one_byte(void)
{
    uint8_t pub[32], msg[1] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_pubkey(pub));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, 1u, T1_SIG, pub));
}

/* Kills: the inversion dropped or flipped, with the slot's own key. */
static void test_the_slot_key_refuses_a_flipped_signature(void)
{
    uint8_t pub[32], sig[64], msg[1] = { 0 };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_pubkey(pub));
    memcpy(sig, T1_SIG, sizeof sig);
    sig[32] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(msg, 0u, sig, pub));
}

/* COSE alg -8, EdDSA (RFC 9053 section 2.2), as its CBOR encoding 0x27. */
static void test_the_cose_alg_is_eddsa(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x27u, tfw_boot_cose_alg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_provisioned_slot_is_read_out);
    RUN_TEST(test_the_slot_key_verifies_rfc8032_test1);
    RUN_TEST(test_the_slot_key_refuses_test1_over_one_byte);
    RUN_TEST(test_the_slot_key_refuses_a_flipped_signature);
    RUN_TEST(test_the_cose_alg_is_eddsa);
    return UNITY_END();
}
