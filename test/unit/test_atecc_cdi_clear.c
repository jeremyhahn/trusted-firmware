/*
 * The element-root CDI is cleared once no longer needed (TCG DICE HW Req 6.4,
 * row dice-attestation, docs/dice-attestation/engineering.md).
 *
 * The element tier's tfw_crypto_init asks the part for CDI = HMAC(UDS,
 * H(placeholder)) only to learn whether a UDS is provisioned. Two copies of
 * that CDI exist on the host side afterwards, and both must be gone:
 *
 *   - the provider's stack copy, cleared by tfw_secure_zero in
 *     src/crypto/atecc608a/provider.c (tfw_crypto_init);
 *   - the driver's file-static response buffer the HMAC END answer was read
 *     into, cleared by tfw_secure_zero at the end of tfw_atecc_exec in
 *     src/crypto/atecc608a/atecc.c.
 *
 * This suite links without src/core/secure_zero.c and supplies its own
 * tfw_secure_zero, which records what it was handed before wiping it (as
 * test_deviceid_p256.c does). The element is the software model
 * (test/model/atecc608a.c), which also says which buffer the driver read the
 * last data response into, so the driver's copy is checked where it lives.
 *
 * The expected CDI is a literal, computed outside this code from the model's
 * documented test UDS (SHA-256 of "tfwsim-atecc608a TEST UDS v1") and the
 * provider's placeholder (SHA-256 of "tfw-unmeasured-application-v1") with
 * HMAC-SHA-256. A value computed by the code under test would move with it.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "boot_measure.h"
#include "handoff.h"
#include "../../src/core/secure_zero.h"

static const uint8_t EXPECTED_CDI[32] = {
    0x6d, 0xce, 0x7f, 0xd0, 0x3e, 0xfd, 0x1a, 0xec,
    0xef, 0x4c, 0xd1, 0x2a, 0xad, 0xdc, 0xe2, 0xe1,
    0xbd, 0x05, 0xc5, 0x39, 0x7e, 0x86, 0x3e, 0xc9,
    0xa4, 0x7a, 0x69, 0xfb, 0x2d, 0x9a, 0x48, 0x58,
};

/* A CDI tfw-boot might have handed off; any value not all zero will do. */
static const uint8_t HANDED_CDI[32] = {
    0xa5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x5a,
};

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

#define ZERO_MAX  16
#define ZERO_COPY 64
static unsigned zero_calls;
static size_t   zero_len[ZERO_MAX];
static uint8_t  zero_before[ZERO_MAX][ZERO_COPY];
static int      zero_after_ok[ZERO_MAX];

void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t            i;
    int               ok = 1;

    if (zero_calls < ZERO_MAX) {
        zero_len[zero_calls] = n;
        memcpy(zero_before[zero_calls], p, n < ZERO_COPY ? n : ZERO_COPY);
    }
    for (i = 0; i < n; i++) { v[i] = 0u; }
    for (i = 0; i < n; i++) { ok = ok && v[i] == 0u; }
    if (zero_calls < ZERO_MAX) {
        zero_after_ok[zero_calls] = ok;
    }
    zero_calls++;
}

/* Whether a 32-byte zero call was handed exactly this value. */
static int zeroed_value(const uint8_t value[32])
{
    unsigned i;
    for (i = 0; i < zero_calls && i < ZERO_MAX; i++) {
        if (zero_len[i] == 32u && memcmp(zero_before[i], value, 32u) == 0) {
            return zero_after_ok[i];
        }
    }
    return 0;
}

/* How many 32-byte zero calls were handed exactly this value and left zero. */
static unsigned zeroed_count(const uint8_t value[32])
{
    unsigned i;
    unsigned n = 0u;
    for (i = 0; i < zero_calls && i < ZERO_MAX; i++) {
        if (zero_len[i] == 32u && memcmp(zero_before[i], value, 32u) == 0
            && zero_after_ok[i]) {
            n++;
        }
    }
    return n;
}

/* Whether any 32-byte window of buf[0, len) equals the value. */
static int holds(const uint8_t *buf, uint8_t len, const uint8_t value[32])
{
    uint8_t i;
    for (i = 0u; (uint8_t)(i + 32u) <= len; i++) {
        if (memcmp(&buf[i], value, 32u) == 0) { return 1; }
    }
    return 0;
}

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    zero_calls = 0u;
}

void tearDown(void) {}

/* Mutation: deleting `tfw_secure_zero(cdi, sizeof cdi);` in provider.c's
 * tfw_crypto_init fails this. */
void test_provider_clears_its_cdi_copy(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    /* The derivation happened: the element answered with a UDS in place. */
    TEST_ASSERT_TRUE(tfw_model_log_count() >= 3u);
    TEST_ASSERT_TRUE(zeroed_value(EXPECTED_CDI));
}

/* Mutation: deleting `tfw_secure_zero(buf, sizeof buf);` at the end of
 * atecc.c's tfw_atecc_exec fails this. */
void test_driver_buffer_holds_no_cdi_after_init(void)
{
    const uint8_t *rd;
    uint8_t        len = 0u;
    uint8_t        i;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());

    /* The last data response the driver read was the HMAC END answer:
     * count, 32 CDI bytes, CRC. Nothing read after it (the sleep is a write). */
    rd = tfw_model_last_data_read(&len);
    TEST_ASSERT_NOT_NULL(rd);
    TEST_ASSERT_EQUAL_UINT8(35u, len);
    TEST_ASSERT_FALSE(holds(rd, len, EXPECTED_CDI));
    for (i = 0u; i < len; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, rd[i]);
    }
}

/* The model's recording is not a vacuous pass: the driver's buffer really did
 * carry the CDI before the clear. Seen through the recorder, which copies what
 * it is handed -- a zero call whose window holds the CDI is the driver's. */
void test_driver_buffer_carried_the_cdi_until_cleared(void)
{
    unsigned i;
    int      seen = 0;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    for (i = 0; i < zero_calls && i < ZERO_MAX; i++) {
        if (zero_len[i] > 32u
            && holds(zero_before[i],
                     (uint8_t)(zero_len[i] < ZERO_COPY ? zero_len[i] : ZERO_COPY),
                     EXPECTED_CDI)) {
            seen = zero_after_ok[i];
        }
    }
    TEST_ASSERT_TRUE(seen);
}

/* Mutation: deleting `tfw_secure_zero(discard, sizeof discard);` in
 * provider.c's handoff drain fails this. The handoff itself is consumed once,
 * so nothing after init can take the handed CDI back. */
void test_handed_off_cdi_is_drained_and_cleared(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t measurement[32];
    uint8_t taken[32];

    memset(measurement, 0x33, sizeof measurement);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SECURE_ELEMENT, HANDED_CDI,
                           measurement, 1u, 0u);
    memcpy(tfw_hal_handoff_block(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    /* Two clears of that value: src/core/handoff.c's own copy, wiped as
     * tfw_boot_handoff_take_cdi hands it out, and the provider's `discard`,
     * the copy it was handed. */
    TEST_ASSERT_EQUAL_UINT(2u, zeroed_count(HANDED_CDI));
    TEST_ASSERT_EQUAL(0, tfw_boot_handoff_take_cdi(taken));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_provider_clears_its_cdi_copy);
    RUN_TEST(test_driver_buffer_holds_no_cdi_after_init);
    RUN_TEST(test_driver_buffer_carried_the_cdi_until_cleared);
    RUN_TEST(test_handed_off_cdi_is_drained_and_cleared);
    return UNITY_END();
}
