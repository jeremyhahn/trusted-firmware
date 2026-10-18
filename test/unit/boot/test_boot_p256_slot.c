/*
 * tfw-boot's P-256 key slot as it ships (boot/verify/p256_key_slot.c), linked
 * with BOOT_SIG=p256-soft's backend in place of test_boot_p256_soft's writable
 * key. The slot is erased flash until the provisioning jig patches it, and an
 * unprovisioned bootloader refuses every image, a valid signature included.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "boot_verify.h"
#include "tfw/progmem.h"
#include "wycheproof_p256.h"

extern const uint8_t tfw_boot_p256_key[64] TFW_PROGMEM;

void setUp(void) {}
void tearDown(void) {}

static uint16_t first_valid_of_key0(void)
{
    uint16_t i;

    for (i = 0u; i < WYC_NVECS; i++) {
        if (wyc_vecs[i].key == 0u && wyc_vecs[i].valid) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("no valid vector for key 0");
    return 0u;
}

static void test_the_shipped_slot_is_erased_flash(void)
{
    uint8_t erased[64];

    memset(erased, 0xFF, sizeof erased);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(erased, tfw_boot_p256_key, 64);
}

static void test_the_shipped_slot_refuses_a_valid_signature(void)
{
    uint16_t v = first_valid_of_key0();

    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                         wyc_vecs[v].len,
                                                         wyc_vecs[v].sig, 0));
}

static void test_the_shipped_slot_still_refuses_a_supplied_key(void)
{
    uint16_t v = first_valid_of_key0();

    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_verify(&wyc_msgs[wyc_vecs[v].off],
                                                              wyc_vecs[v].len,
                                                              wyc_vecs[v].sig,
                                                              wyc_keys[0]));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_shipped_slot_is_erased_flash);
    RUN_TEST(test_the_shipped_slot_refuses_a_valid_signature);
    RUN_TEST(test_the_shipped_slot_still_refuses_a_supplied_key);
    return UNITY_END();
}
