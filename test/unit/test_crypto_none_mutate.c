/*
 * Mutation tests for src/crypto/none/provider.c (#163). test_crypto_none
 * kills every mutant of the refusals and of the drain itself; the one it
 * cannot see is the drained copy left on the stack unzeroed. This link has
 * no src/core/secure_zero.c: the suite's own tfw_secure_zero records each
 * buffer it is handed before it zeroes it.
 */
#include <string.h>
#include "unity.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "hal_mock.h"
#include "handoff.h"
#include "boot_measure.h"

static uint8_t cdi[32];
static unsigned cdi_zeroed;     /* calls handed a 32-byte buffer holding cdi */

void tfw_secure_zero(void *p, size_t n)
{
    if (n == sizeof cdi && memcmp(p, cdi, sizeof cdi) == 0) {
        cdi_zeroed++;
    }
    memset(p, 0, n);
}

void setUp(void)
{
    cdi_zeroed = 0u;
}

void tearDown(void) {}

/* Both copies of a handed-off CDI are zeroed: the handoff's own, and the
 * one this tier drained it into. */
void test_init_zeroes_the_cdi_it_drains(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t measurement[32];
    uint8_t i;

    for (i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0x5Au ^ i); }
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)(0xA0u + i); }
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    cdi_zeroed = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(2u, cdi_zeroed);
}

/* No handoff, nothing drained, nothing to zero. */
void test_init_with_no_handoff_zeroes_nothing(void)
{
    memset(tfw_mock_handoff(), 0, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
    cdi_zeroed = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(0u, cdi_zeroed);
    TEST_ASSERT_EQUAL(TFW_CRYPTO_NONE, tfw_crypto_tier());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_zeroes_the_cdi_it_drains);
    RUN_TEST(test_init_with_no_handoff_zeroes_nothing);
    return UNITY_END();
}
