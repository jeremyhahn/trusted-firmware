/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build's image sealer (tools/image-seal/, row ota) against tfw-boot's
 * own verdict: an image sealed here, its span signed as go-xkms signs it
 * (ES256 over SHA-256 of the 48 bytes), boots under tfw_boot_evaluate with the
 * p256-soft backend -- and the stream the stager takes installs as the same
 * region. Built with the ATmega2560's profile, the board of the soft row.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "seal.h"
#include "boot_hal_mock.h"
#include "boot_image.h"
#include "boot_policy.h"
#include "sha256.h"
#include "tfw/config.h"
#include "uECC.h"
#include "vectors.h"

/* The host test's key slot, in place of p256_key_slot.c, as in
 * test_boot_p256_soft.c. */
uint8_t tfw_boot_p256_key[64];

/* boot_harness.c's EL_SIGNING_KEY, test_boot_p256_soft.c's el_signing_key: a
 * test key standing in for go-xkms's. Signed at test time; nothing pinned. */
static const uint8_t signing_key[32] = {
    0x9E,0x1C,0x4B,0x22,0x70,0xA5,0x33,0xF8,0x0D,0x61,0xB4,0x17,0xCA,0x2E,0x88,0x5D,
    0x30,0x79,0xE6,0x44,0x12,0xBB,0x57,0x9A,0xC3,0x08,0x6F,0xD1,0x25,0xAE,0x73,0x40
};

typedef struct { uECC_HashContext uecc; tfw_sha256_t ctx; } hash_ctx_t;

static hash_ctx_t *as_ctx(const uECC_HashContext *c) { return (hash_ctx_t *)(uintptr_t)c; }
static void h_init(const uECC_HashContext *c) { tfw_sha256_init(&as_ctx(c)->ctx); }
static void h_update(const uECC_HashContext *c, const uint8_t *m, unsigned n)
{
    tfw_sha256_update(&as_ctx(c)->ctx, m, (uint16_t)n);
}
static void h_finish(const uECC_HashContext *c, uint8_t *out)
{
    tfw_sha256_final(&as_ctx(c)->ctx, out);
}

/* What go-xkms returns for a span: r || s over its SHA-256. */
static void sign_span(const uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN], uint8_t sig[TFW_BOOT_SIG_LEN])
{
    uint8_t    digest[32];
    uint8_t    tmp[32 + 32 + 64];
    hash_ctx_t hc;

    tfw_sha256(tbs, TFW_BOOT_TR_SIGNED_LEN, digest);
    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    TEST_ASSERT_EQUAL_INT(1, uECC_sign_deterministic(signing_key, digest, 32u, &hc.uecc,
                                                     sig, uECC_secp256r1()));
}

#define IMAGE_LEN 1000u

static uint8_t image[TFW_BOOT_APP_SIZE];
static uint8_t region[TFW_BOOT_APP_SIZE];
static uint8_t stream[TFW_BOOT_APP_SIZE];

void setUp(void)
{
    uint32_t i;

    tfw_boot_mock_reset();
    for (i = 0u; i < sizeof image; i++) {
        image[i] = (uint8_t)(i * 7u + 3u);
    }
    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(signing_key, tfw_boot_p256_key,
                                                     uECC_secp256r1()));
}
void tearDown(void) {}

/* tbs, then go-xkms's signature, then the trailer. */
static void seal(uint32_t len, uint32_t version, uint8_t trailer[TFW_BOOT_TRAILER_LEN])
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN], sig[TFW_BOOT_SIG_LEN];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, len, version, 0u, tbs));
    sign_span(tbs, sig);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_trailer(image, len, tbs, sig, trailer));
}

static tfw_status_t boot_region(uint32_t floor)
{
    tfw_boot_verdict_t v;

    memcpy(&tfw_boot_mock_flash()[TFW_BOOT_APP_BASE], region, sizeof region);
    return tfw_boot_evaluate(floor, &v);
}

/* testdata/vectors.h's span for this board: image_version 3, image_len 4096.
 * Its measurement is a fixed value, not a hash of 4096 bytes, so the header is
 * compared there and the measurement against the image here. */
static void test_the_span_is_the_wire_vector_for_this_board(void)
{
    static const uint8_t wire[] = TFWV_TRAILER_ATMEGA2560_SPAN;
    uint8_t              tbs[TFW_BOOT_TR_SIGNED_LEN], m[32];

    TEST_ASSERT_EQUAL_size_t(TFWV_TRAILER_SPAN_LEN, sizeof tbs);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, 4096u, 3u, 0u, tbs));
    TEST_ASSERT_EQUAL_MEMORY(wire, tbs, TFW_BOOT_TR_MEASUREMENT);
    TEST_ASSERT_EQUAL_HEX8(TFW_BOARD_ID, tbs[TFW_BOOT_TR_BOARD_ID]);
    tfw_sha256(image, 4096u, m);
    TEST_ASSERT_EQUAL_MEMORY(m, &tbs[TFW_BOOT_TR_MEASUREMENT], sizeof m);
}

static void test_a_sealed_region_boots_under_tfw_boot(void)
{
    uint8_t trailer[TFW_BOOT_TRAILER_LEN];

    seal(IMAGE_LEN, 7u, trailer);
    tfw_seal_region(image, IMAGE_LEN, trailer, region);
    TEST_ASSERT_EQUAL_INT(TFW_OK, boot_region(7u));
}

static void test_a_sealed_region_below_the_floor_is_refused(void)
{
    uint8_t trailer[TFW_BOOT_TRAILER_LEN];

    seal(IMAGE_LEN, 7u, trailer);
    tfw_seal_region(image, IMAGE_LEN, trailer, region);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, boot_region(8u));
}

static void test_one_byte_of_a_sealed_image_changed_is_refused(void)
{
    uint8_t trailer[TFW_BOOT_TRAILER_LEN];

    seal(IMAGE_LEN, 7u, trailer);
    tfw_seal_region(image, IMAGE_LEN, trailer, region);
    region[IMAGE_LEN - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, boot_region(7u));
}

/* tfw_boot_stage_copy's rule: the stream's body to the region's base, its
 * last TFW_BOOT_TRAILER_LEN bytes to the region's end. */
static void test_the_stream_installs_as_the_region(void)
{
    static uint8_t installed[TFW_BOOT_APP_SIZE];
    uint8_t        trailer[TFW_BOOT_TRAILER_LEN];
    uint32_t       total = tfw_seal_stream_len(IMAGE_LEN, 0u);
    uint32_t       body  = total - TFW_BOOT_TRAILER_LEN;

    seal(IMAGE_LEN, 7u, trailer);
    tfw_seal_stream(image, IMAGE_LEN, NULL, 0u, trailer, stream);
    tfw_seal_region(image, IMAGE_LEN, trailer, region);

    TEST_ASSERT_EQUAL_UINT32(0u, total % TFW_OTA_PAGE_MAX);
    TEST_ASSERT_EQUAL_MEMORY(trailer, &stream[body], TFW_BOOT_TRAILER_LEN);
    memset(installed, 0xFF, sizeof installed);
    memcpy(installed, stream, body);
    memcpy(&installed[TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE], &stream[body],
           TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL_MEMORY(region, installed, sizeof installed);
}

static void test_the_stream_is_page_padded_with_the_trailer_last(void)
{
    TEST_ASSERT_EQUAL_UINT32(256u, tfw_seal_stream_len(1u, 0u));
    TEST_ASSERT_EQUAL_UINT32(256u, tfw_seal_stream_len(128u, 0u));
    TEST_ASSERT_EQUAL_UINT32(512u, tfw_seal_stream_len(129u, 0u));
    TEST_ASSERT_EQUAL_UINT32(1280u, tfw_seal_stream_len(IMAGE_LEN, 0u));
    /* The largest image the trailer admits fills the staging area exactly. */
    TEST_ASSERT_EQUAL_UINT32(TFW_BOOT_APP_SIZE, tfw_seal_stream_len(TFW_BOOT_MAX_IMAGE_LEN, 0u));
    TEST_ASSERT_TRUE(tfw_seal_stream_len(TFW_BOOT_MAX_IMAGE_LEN, 0u) <= TFW_OTA_STORE_STAGE_LEN);
}

/* An envelope's two length bytes and its bytes count before the trailer: 1000
 * + 2 + 150 + 128 still fits 1280, 1000 + 2 + 151 + 128 takes another page. */
static void test_an_envelope_counts_in_the_stream_length(void)
{
    TEST_ASSERT_EQUAL_UINT32(1280u, tfw_seal_stream_len(IMAGE_LEN, 150u));
    TEST_ASSERT_EQUAL_UINT32(1536u, tfw_seal_stream_len(IMAGE_LEN, 151u));
    TEST_ASSERT_EQUAL_UINT32(1536u, tfw_seal_stream_len(IMAGE_LEN, 320u));
}

/* The layout OTA_ACTIVATE's manifest check reads (ruling 121), as literals:
 * the image, the length 0x0103 little-endian, the envelope, 0xFF, the trailer
 * last; and the region, which tfw-boot reads, without the envelope. */
static void test_the_envelope_follows_the_image_with_its_length(void)
{
    static const uint8_t head[6] = { 0x03u, 0x01u, 0x84u, 0x85u, 0x86u, 0x87u };
    static uint8_t       env[259];
    uint8_t              trailer[TFW_BOOT_TRAILER_LEN];
    uint32_t             i;

    for (i = 0u; i < sizeof env; i++) {
        env[i] = (uint8_t)(i + 0x84u);
    }
    seal(IMAGE_LEN, 7u, trailer);
    tfw_seal_stream(image, IMAGE_LEN, env, (uint16_t)sizeof env, trailer, stream);
    tfw_seal_region(image, IMAGE_LEN, trailer, region);

    TEST_ASSERT_EQUAL_MEMORY(image, stream, IMAGE_LEN);
    TEST_ASSERT_EQUAL_MEMORY(head, &stream[IMAGE_LEN], sizeof head);
    TEST_ASSERT_EQUAL_HEX8(0x86u, stream[IMAGE_LEN + 2u + 258u]);
    for (i = IMAGE_LEN + 2u + 259u; i < 1536u - TFW_BOOT_TRAILER_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, stream[i]);
    }
    TEST_ASSERT_EQUAL_MEMORY(trailer, &stream[1536u - TFW_BOOT_TRAILER_LEN], TFW_BOOT_TRAILER_LEN);
    for (i = IMAGE_LEN; i < TFW_BOOT_TRAILER_ADDR - TFW_BOOT_APP_BASE; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, region[i]);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, boot_region(7u));
}

static void test_the_largest_image_boots(void)
{
    uint8_t trailer[TFW_BOOT_TRAILER_LEN];

    seal(TFW_BOOT_MAX_IMAGE_LEN, 1u, trailer);
    tfw_seal_region(image, TFW_BOOT_MAX_IMAGE_LEN, trailer, region);
    TEST_ASSERT_EQUAL_INT(TFW_OK, boot_region(1u));
}

static void test_what_tfw_boot_refuses_is_never_spanned(void)
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN];

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_seal_tbs(image, 0u, 7u, 0u, tbs));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_seal_tbs(image, TFW_BOOT_MAX_IMAGE_LEN + 1u, 7u, 0u, tbs));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_seal_tbs(image, IMAGE_LEN, 0u, 0u, tbs));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_seal_tbs(image, IMAGE_LEN, 7u, 0x02u, tbs));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, IMAGE_LEN, 7u, TFW_BOOT_IMAGE_FLAG_DEV, tbs));
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_IMAGE_FLAG_DEV, tbs[TFW_BOOT_TR_IMAGE_FLAGS]);
}

static void test_a_span_for_another_image_is_refused(void)
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN], sig[TFW_BOOT_SIG_LEN];
    uint8_t trailer[TFW_BOOT_TRAILER_LEN];

    memset(trailer, 0xA5, sizeof trailer);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, IMAGE_LEN, 7u, 0u, tbs));
    sign_span(tbs, sig);

    /* Another length. */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_seal_trailer(image, IMAGE_LEN + 1u, tbs, sig, trailer));
    /* Another byte. */
    image[3] ^= 0x80u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_seal_trailer(image, IMAGE_LEN, tbs, sig, trailer));
    image[3] ^= 0x80u;
    /* Another board: the 328P's id in byte 6. */
    tbs[TFW_BOOT_TR_BOARD_ID] = 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_seal_trailer(image, IMAGE_LEN, tbs, sig, trailer));
    /* Not a span at all. */
    tbs[TFW_BOOT_TR_BOARD_ID] = (uint8_t)TFW_BOARD_ID;
    tbs[TFW_BOOT_TR_MAGIC] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_seal_trailer(image, IMAGE_LEN, tbs, sig, trailer));

    /* Nothing written by any refusal. */
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5u, trailer, sizeof trailer);
}

/* A signature over another span seals -- no public key is in the tree -- and
 * tfw-boot refuses it: the device is where the signature is checked. */
static void test_a_signature_over_another_span_does_not_boot(void)
{
    uint8_t tbs[TFW_BOOT_TR_SIGNED_LEN], other[TFW_BOOT_TR_SIGNED_LEN];
    uint8_t sig[TFW_BOOT_SIG_LEN], trailer[TFW_BOOT_TRAILER_LEN];

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, IMAGE_LEN, 7u, 0u, tbs));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_tbs(image, IMAGE_LEN, 8u, 0u, other));
    sign_span(other, sig);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_seal_trailer(image, IMAGE_LEN, tbs, sig, trailer));
    tfw_seal_region(image, IMAGE_LEN, trailer, region);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, boot_region(7u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_span_is_the_wire_vector_for_this_board);
    RUN_TEST(test_a_sealed_region_boots_under_tfw_boot);
    RUN_TEST(test_a_sealed_region_below_the_floor_is_refused);
    RUN_TEST(test_one_byte_of_a_sealed_image_changed_is_refused);
    RUN_TEST(test_the_stream_installs_as_the_region);
    RUN_TEST(test_the_stream_is_page_padded_with_the_trailer_last);
    RUN_TEST(test_an_envelope_counts_in_the_stream_length);
    RUN_TEST(test_the_envelope_follows_the_image_with_its_length);
    RUN_TEST(test_the_largest_image_boots);
    RUN_TEST(test_what_tfw_boot_refuses_is_never_spanned);
    RUN_TEST(test_a_span_for_another_image_is_refused);
    RUN_TEST(test_a_signature_over_another_span_does_not_boot);
    return UNITY_END();
}
