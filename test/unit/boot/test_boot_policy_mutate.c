/*
 * boot/boot_policy.c under mutation (#163).
 *
 * test_boot_policy runs the policy against a real signature primitive. This
 * suite runs it against a stand-in verify that RECORDS what it was handed, so
 * each fault below is seen where it happens rather than as a signature that
 * happens to fail. The stand-in is not a crypto row: it accepts a "signature"
 * that is SHA-256 of the message followed by the key, which is enough to tell
 * the span, the key and the verdict apart, and nothing else.
 *
 * Mutants this suite kills (each tried against the unit and seen to fail it):
 *   P1  tfw_boot_equal: `diff |=` -> `diff =` (only the last byte counts)
 *   P2  tfw_boot_equal: the loop stops one byte short
 *   P3  tfw_boot_check_pubkey_pin: the comparison's `!` dropped
 *   P4  tfw_boot_evaluate_at: the trailer read at TFW_BOOT_TRAILER_ADDR, not
 *       at `base`'s
 *   P5  tfw_boot_evaluate_at: the body measured from TFW_BOOT_APP_BASE, not
 *       from `base`
 *   P6  the board binding dropped
 *   P7  the floor check dropped, or handed 0 for the floor
 *   P8  the floor checked after the measurement
 *   P9  the measurement comparison dropped
 *   P10 the pin read from the block's start, not its pubkey_hash
 *   P11 the stored-key tier: `key = 0` dropped, or UNSUPPORTED returned
 *   P12 the verify span one byte short, or verify's verdict replaced by OK
 *   P13 the trailer read stops after its first half
 */
#include <string.h>
#include "unity.h"
#include "boot_policy.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_platform.h"
#include "boot_hal_mock.h"
#include "sha256.h"

#define IMAGE_LEN 2048u

static uint8_t      g_pub[TFW_BOOT_PUBKEY_LEN];
static tfw_status_t g_pubkey_st;
static tfw_status_t g_stored_verdict;
static tfw_status_t g_verdict_override;
static unsigned     g_calls;
static unsigned     g_null_key;
static uint16_t     g_msglen;
static uint8_t      g_msg[TFW_BOOT_TRAILER_LEN];

tfw_status_t tfw_boot_pubkey(uint8_t out[TFW_BOOT_PUBKEY_LEN])
{
    if (g_pubkey_st != TFW_OK) {
        return g_pubkey_st;
    }
    memcpy(out, g_pub, TFW_BOOT_PUBKEY_LEN);
    return TFW_OK;
}

tfw_status_t tfw_boot_verify(const uint8_t *msg, uint16_t msglen,
                           const uint8_t sig[TFW_BOOT_SIG_LEN],
                           const uint8_t pub[TFW_BOOT_PUBKEY_LEN])
{
    uint8_t h[32];

    g_calls++;
    g_msglen = msglen;
    memcpy(g_msg, msg, (msglen < sizeof g_msg) ? msglen : sizeof g_msg);
    if (pub == 0) {
        g_null_key++;
        return g_stored_verdict;
    }
    if (g_verdict_override != TFW_OK) {
        return g_verdict_override;
    }
    tfw_sha256(msg, msglen, h);
    if (memcmp(h, sig, 32u) != 0 || memcmp(pub, sig + 32, 32u) != 0) {
        return TFW_ERR_CRYPTO;
    }
    return TFW_OK;
}

/* An image at `base`, its trailer at the end of the region that starts there,
 * "signed" for the stand-in, and the key's hash pinned in the block. */
static void install_at(uint32_t base, uint32_t version, uint8_t board_id)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  raw[TFW_BOOT_TRAILER_LEN];
    uint8_t  ee[TFW_BOOT_EE_LEN];
    tfw_boot_trailer_t  tr;
    tfw_boot_platform_t plat;

    for (uint16_t i = 0u; i < IMAGE_LEN; i++) {
        flash[base + i] = (uint8_t)(0xA5u ^ (uint8_t)i ^ (uint8_t)(i >> 8));
    }
    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = IMAGE_LEN;
    tr.board_id      = board_id;
    tfw_sha256(flash + base, IMAGE_LEN, tr.measurement);
    tfw_boot_trailer_serialise(&tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, &raw[TFW_BOOT_TR_SIGNATURE]);
    memcpy(&raw[TFW_BOOT_TR_SIGNATURE + 32u], g_pub, 32u);
    memcpy(flash + base + (TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE), raw, TFW_BOOT_TRAILER_LEN);

    memset(ee, 0xFF, sizeof ee);
    memset(&plat, 0, sizeof plat);
    tfw_boot_hash(g_pub, TFW_BOOT_PUBKEY_LEN, plat.pubkey_hash);
    plat.boot_state = TFW_BOOT_STATE_GOOD;
    tfw_boot_platform_serialise(&plat, ee);
    memcpy(tfw_boot_mock_nvm(), ee, TFW_BOOT_EE_LEN);
}

static void install(uint32_t version)
{
    install_at(TFW_BOOT_APP_BASE, version, (uint8_t)TFW_BOARD_ID);
}

/* Rewrite the pinned hash under a correct CRC. */
static void pin(const uint8_t hash[TFW_BOOT_MEASURE_LEN])
{
    tfw_boot_platform_t p;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_platform_parse(tfw_boot_mock_nvm(), &p));
    memcpy(p.pubkey_hash, hash, TFW_BOOT_MEASURE_LEN);
    tfw_boot_platform_serialise(&p, tfw_boot_mock_nvm());
}

void setUp(void)
{
    tfw_boot_mock_reset();
    for (uint8_t i = 0u; i < TFW_BOOT_PUBKEY_LEN; i++) {
        g_pub[i] = (uint8_t)(0x30u + i);
    }
    g_pubkey_st        = TFW_OK;
    g_stored_verdict   = TFW_OK;
    g_verdict_override = TFW_OK;
    g_calls            = 0u;
    g_null_key         = 0u;
    g_msglen           = 0u;
}

void tearDown(void) {}

/* P12: the span verify sees is the trailer's signed prefix as it sits in
 * flash, all of it, with the device's key; and its verdict is the policy's. */
static void test_verify_gets_the_signed_prefix_and_its_verdict_stands(void)
{
    tfw_boot_verdict_t v;
    const uint8_t *tr = tfw_boot_mock_flash() + TFW_BOOT_TRAILER_ADDR;

    install(3u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(1u, g_calls);
    TEST_ASSERT_EQUAL_UINT(0u, g_null_key);
    TEST_ASSERT_EQUAL_UINT16(TFW_BOOT_TR_SIGNED_LEN, g_msglen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tr, g_msg, TFW_BOOT_TR_SIGNED_LEN);

    g_verdict_override = TFW_ERR_INTERNAL;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, tfw_boot_evaluate(0u, &v));
}

/* P12, and the second half of the trailer read: one bit of the signature's
 * last byte, in that half, refuses. */
static void test_the_last_signature_byte_is_read(void)
{
    tfw_boot_verdict_t v;

    install(3u);
    tfw_boot_mock_flash()[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_SIGNATURE + TFW_BOOT_SIG_LEN - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
}

/* P4, P5: an image in a second region is evaluated where it is. The primary
 * region is blank, so a trailer or a body read from it cannot pass. */
static void test_evaluate_at_reads_trailer_and_body_at_its_base(void)
{
    tfw_boot_verdict_t v;
    uint8_t            expect[32];
    const uint32_t     base = (uint32_t)tfw_boot_mock_flash_size() - TFW_BOOT_APP_SIZE;

    TEST_ASSERT_TRUE(base > TFW_BOOT_APP_BASE);
    install_at(base, 4u, (uint8_t)TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate_at(base, 0u, &v));
    tfw_sha256(tfw_boot_mock_flash() + base, IMAGE_LEN, expect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, v.measurement, 32u);
    TEST_ASSERT_EQUAL_UINT32(4u, v.trailer.image_version);

    tfw_boot_mock_flash()[base + IMAGE_LEN - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate_at(base, 0u, &v));
}

/* P6: another board's image, correctly "signed": refused before verify. */
static void test_the_binding_refuses_before_anything_is_verified(void)
{
    tfw_boot_verdict_t v;

    install_at(TFW_BOOT_APP_BASE, 3u, (uint8_t)(TFW_BOARD_ID + 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_calls);
}

/* P7: the floor is the caller's, at its edges. */
static void test_the_floor_is_the_callers_at_its_edges(void)
{
    tfw_boot_verdict_t v;

    install(5u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(5u, &v));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_evaluate(6u, &v));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_evaluate(0xFFFFFFFFuL, &v));
    TEST_ASSERT_EQUAL_UINT(1u, g_calls);
}

/* P8: below the floor AND tampered names the floor -- nothing is measured
 * for an image the floor already refuses. */
static void test_the_floor_is_checked_before_the_measurement(void)
{
    tfw_boot_verdict_t v;

    install(2u);
    tfw_boot_mock_flash()[100] ^= 0xFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_evaluate(3u, &v));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(2u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_calls);
}

/* P9: a body changed under a trailer whose signature still holds. Only the
 * measurement comparison stands between this and a boot. */
static void test_a_body_change_under_a_valid_signature_is_refused(void)
{
    tfw_boot_verdict_t v;

    install(3u);
    tfw_boot_mock_flash()[0] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
    install(3u);
    tfw_boot_mock_flash()[IMAGE_LEN - 1u] ^= 0x80u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_calls);
}

/* P3, P10, P1, P2: the pin is compared, every byte of it, where it sits. */
static void test_the_pin_is_every_byte_of_the_pubkey_hash(void)
{
    tfw_boot_verdict_t v;
    uint8_t            h[TFW_BOOT_MEASURE_LEN];

    install(3u);
    tfw_boot_hash(g_pub, TFW_BOOT_PUBKEY_LEN, h);
    h[0] ^= 0x01u;
    pin(h);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, tfw_boot_evaluate(0u, &v));
    h[0] ^= 0x01u;
    h[TFW_BOOT_MEASURE_LEN - 1u] ^= 0x80u;
    pin(h);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(0u, g_calls);

    h[TFW_BOOT_MEASURE_LEN - 1u] ^= 0x80u;
    pin(h);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
}

/* P11: the element tier keeps its key. Verify gets none, whatever the pin
 * holds, and its verdict is the policy's. */
static void test_a_kept_key_is_verified_with_no_key_and_no_pin(void)
{
    tfw_boot_verdict_t v;
    uint8_t            h[TFW_BOOT_MEASURE_LEN];

    install(3u);
    memset(h, 0x00, sizeof h);
    pin(h);
    g_pubkey_st = TFW_ERR_UNSUPPORTED;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(1u, g_null_key);
    g_stored_verdict = TFW_ERR_CRYPTO;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(2u, g_null_key);

    g_pubkey_st = TFW_ERR_BAD_STATE;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_evaluate(0u, &v));
    TEST_ASSERT_EQUAL_UINT(2u, g_calls);
}

/* P1, P2: equal reports 1 for equal spans and 0 for a difference at any
 * position, and an empty span is equal. */
static void test_equal_sees_a_difference_at_every_position(void)
{
    uint8_t a[TFW_BOOT_MEASURE_LEN];
    uint8_t b[TFW_BOOT_MEASURE_LEN];

    memset(a, 0x5Cu, sizeof a);
    memcpy(b, a, sizeof b);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_equal(a, b, sizeof a));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_equal(a, b, 0u));
    for (uint8_t i = 0u; i < sizeof a; i++) {
        b[i] ^= (uint8_t)(1u << (i % 8u));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_equal(a, b, sizeof a));
        b[i] = a[i];
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_verify_gets_the_signed_prefix_and_its_verdict_stands);
    RUN_TEST(test_the_last_signature_byte_is_read);
    RUN_TEST(test_evaluate_at_reads_trailer_and_body_at_its_base);
    RUN_TEST(test_the_binding_refuses_before_anything_is_verified);
    RUN_TEST(test_the_floor_is_the_callers_at_its_edges);
    RUN_TEST(test_the_floor_is_checked_before_the_measurement);
    RUN_TEST(test_a_body_change_under_a_valid_signature_is_refused);
    RUN_TEST(test_the_pin_is_every_byte_of_the_pubkey_hash);
    RUN_TEST(test_a_kept_key_is_verified_with_no_key_and_no_pin);
    RUN_TEST(test_equal_sees_a_difference_at_every_position);
    return UNITY_END();
}
