/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Mutants of the BOOT_SIG=p256-soft key slot, boot/verify/p256_key_slot.c
 * (#130, #163). The unit is one initialised array and no code, so gcov finds
 * no functions in it and no coverage report can list it: its test is what it
 * holds. The slot is compiled into this suite's own unit, so the definition's
 * size is in view: every reader (verify_p256_soft.c, verify_p256_rom.c,
 * verify_digest_soft.c, l0_gate.c) declares 64 bytes for itself, and a slot
 * of any other size would link beside them unseen.
 *
 * Mutants tried, each killed by the test named, or equivalent:
 *   a byte other than 0xFF (first, middle, last)   test_the_slot_is_erased_flash
 *   63 initialisers (the last byte 0)               test_the_slot_is_erased_flash
 *   the first bytes a point's X                     test_the_slot_is_erased_flash
 *   the array 65 or 63 bytes                        test_the_slot_is_one_p256_point
 *   Equivalent: TFW_PROGMEM dropped (it is empty on the host; the AVR link
 *   places the slot, which no host build can see).
 */
#include <stdint.h>
#include "unity.h"
#include "verify/p256_key_slot.c"

void setUp(void) {}
void tearDown(void) {}

/* The uncompressed point X || Y, 64 bytes, as the element tier's slot. */
static void test_the_slot_is_one_p256_point(void)
{
    TEST_ASSERT_EQUAL_UINT(64u, sizeof tfw_boot_p256_key);
}

/* Erased flash in every byte: an unprovisioned bootloader holds no key. */
static void test_the_slot_is_erased_flash(void)
{
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFFu, tfw_boot_p256_key, sizeof tfw_boot_p256_key);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_slot_is_one_p256_point);
    RUN_TEST(test_the_slot_is_erased_flash);
    return UNITY_END();
}
