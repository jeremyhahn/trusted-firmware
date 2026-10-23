/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The P4's ROM ECDSA backend (boot/hal/esp32/verify_p256_rom.c) under seeded
 * mutation (#163). test_boot_p256_rom.c and the gate suite over this backend
 * killed 17 of 18 mutants; the one left was the COSE algorithm it names.
 *
 * That byte goes into the protected header of the OTA_ACTIVATE authorization
 * (#137), which a host signs: ES256 is COSE algorithm -7 (RFC 9053 section
 * 2.1), CBOR's negative integer 0x26. EdDSA, -8, is 0x27, donna's.
 */

#include <stdint.h>
#include "unity.h"
#include "boot_verify.h"

/* The host test's key slot, in place of the generated one. */
uint8_t tfw_boot_p256_key[64];

void setUp(void) {}
void tearDown(void) {}

static void test_the_backend_names_es256(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x26u, tfw_boot_cose_alg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_backend_names_es256);
    return UNITY_END();
}
