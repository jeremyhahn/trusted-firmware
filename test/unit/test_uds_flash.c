/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/crypto/soft/uds_flash.c, the image's UDS source, linked in place of
 * test/mock/uds_mock.c. Until tfw-boot hands one over it has no UDS, so the
 * soft provider it feeds stays unprovisioned and refuses to sign.
 */
#include <string.h>
#include "unity.h"
#include "tfw/crypto.h"

const uint8_t *tfw_crypto_soft_uds(void);

void setUp(void)    {}
void tearDown(void) {}

void test_the_image_has_no_uds_of_its_own(void)
{
    TEST_ASSERT_NULL(tfw_crypto_soft_uds());
}

void test_the_soft_root_without_a_uds_refuses_to_sign(void)
{
    uint8_t digest[32], sig[64], alg = 0u;

    memset(digest, 0x5A, sizeof digest);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_sign(digest, sig, &alg));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_image_has_no_uds_of_its_own);
    RUN_TEST(test_the_soft_root_without_a_uds_refuses_to_sign);
    return UNITY_END();
}
