/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 layer 0 gate (boot/hal/esp32/l0_gate.c) under seeded mutation
 * (#163). Each test is aimed at a mutant test_boot_l0_esp32.c let through:
 * a slot with exactly the trailer's room, a trailer whose board or
 * measurement is wrong but signed, an EEPROM that does not read, and an
 * envelope length that is zero, one past the buffer or past the slot.
 *
 * The fixture is test_boot_l0_esp32.c's: a 1000-byte image, its ESP-IDF
 * digest, a trailer and a SUIT envelope signed with the image key that suite
 * already carries; the public half is computed here.
 *
 * Named equivalent: esp_len > slot_size against >= (at equality the room
 * left is 0, which the trailer bound refuses with the same status), an
 * envelope length of 0 let through (tfw_suit_gate refuses an empty envelope
 * with the same TFW_ERR_RANGE, and nothing is read), and the envelope's read
 * chunk of 128 against 127 (the bytes read are the same).
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_platform.h"
#include "boot_verify.h"
#include "sha256.h"
#include "uECC.h"
#include "emit.h"
#include "l0_gate.h"
#include "suit.h"
#include "tfw/config.h"

/* The host test's key slot, in place of the generated one. */
uint8_t tfw_boot_p256_key[64];

#define BASE      0x0400u
#define SLOT      0x2000u
#define IMAGE     1000u
#define ESP_LEN   (IMAGE + 32u)
#define ENV_AT    (BASE + ESP_LEN + TFW_BOOT_TRAILER_LEN)

static const uint8_t image_key[32] = {
    0x5A, 0x13, 0xC7, 0x2E, 0x91, 0x0B, 0x64, 0xF2, 0x38, 0xAD, 0x7E, 0x46, 0x09, 0xD5, 0x6C, 0x81,
    0x22, 0xB9, 0x4F, 0xE0, 0x17, 0x8A, 0x3D, 0x56, 0xCB, 0x70, 0x05, 0x9E, 0x63, 0xF4, 0x28, 0xB1,
};

static uint8_t digest[32];

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

static void sign(const uint8_t key[32], const uint8_t h[32], uint8_t sig[64])
{
    uint8_t    tmp[32 + 32 + 64];
    hash_ctx_t hc;

    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    TEST_ASSERT_EQUAL_INT(1, uECC_sign_deterministic(key, h, 32u, &hc.uecc, sig,
                                                     uECC_secp256r1()));
}

static void put_trailer(const tfw_boot_trailer_t *tr)
{
    uint8_t *raw = &tfw_boot_mock_flash()[BASE + ESP_LEN];
    uint8_t  h[32];

    tfw_boot_trailer_serialise(tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, h);
    sign(image_key, h, &raw[TFW_BOOT_TR_SIGNATURE]);
}

static const uint8_t vendor_id[16] = TFW_SUIT_VENDOR_ID;
static const uint8_t class_id[16]  = TFW_SUIT_CLASS_ID;
static uint16_t      env_len;

static void put_envelope(const uint8_t image_digest[32])
{
    uint8_t *at = &tfw_boot_mock_flash()[ENV_AT];
    uint8_t  man[160];
    uint8_t  tbs[96];
    uint8_t  h[32];
    uint8_t  sig[64];
    size_t   mlen;
    size_t   tlen;

    mlen = tfw_suit_emit_manifest(man, sizeof man, image_digest, IMAGE, 3u, vendor_id, class_id);
    TEST_ASSERT_NOT_EQUAL(0u, mlen);
    tlen = tfw_suit_emit_tbs(tbs, sizeof tbs, man, mlen);
    TEST_ASSERT_NOT_EQUAL(0u, tlen);
    tfw_sha256(tbs, (uint16_t)tlen, h);
    sign(image_key, h, sig);
    env_len = (uint16_t)tfw_suit_emit_envelope(&at[2], TFW_SUIT_ENVELOPE_MAX, man, mlen, sig);
    TEST_ASSERT_NOT_EQUAL(0u, env_len);
    at[0] = (uint8_t)env_len;
    at[1] = (uint8_t)(env_len >> 8);
}

static void put_env_len(uint16_t len)
{
    uint8_t *at = &tfw_boot_mock_flash()[ENV_AT];

    at[0] = (uint8_t)len;
    at[1] = (uint8_t)(len >> 8);
}

static void good_trailer(tfw_boot_trailer_t *tr)
{
    memset(tr, 0, sizeof *tr);
    tr->image_version = 3u;
    tr->image_len     = IMAGE;
    tr->board_id      = (uint8_t)TFW_BOARD_ID;
    memcpy(tr->measurement, digest, 32u);
}

static void install_key(void)
{
    tfw_boot_platform_t plat;

    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(image_key, tfw_boot_p256_key, uECC_secp256r1()));
    memset(&plat, 0, sizeof plat);
    tfw_sha256(tfw_boot_p256_key, 64u, plat.pubkey_hash);
    tfw_boot_platform_serialise(&plat, &tfw_boot_mock_nvm()[TFW_BOOT_EE_BASE]);
}

void setUp(void)
{
    tfw_boot_trailer_t tr;
    uint8_t           *f;
    uint32_t           i;

    tfw_boot_mock_reset();
    f = tfw_boot_mock_flash();
    for (i = 0u; i < IMAGE; i++) {
        f[BASE + i] = (uint8_t)(i * 7u + 1u);
    }
    tfw_sha256(&f[BASE], IMAGE, digest);
    memcpy(&f[BASE + IMAGE], digest, 32u);
    install_key();
    good_trailer(&tr);
    put_trailer(&tr);
    put_envelope(digest);
}

void tearDown(void) {}

static tfw_status_t gate_in(uint32_t slot, tfw_boot_trailer_t *out)
{
    return tfw_l0_gate(BASE, slot, ESP_LEN, digest, 3u, out);
}

static void test_the_fixture_passes(void)
{
    tfw_boot_trailer_t out;

    TEST_ASSERT_EQUAL_INT(TFW_OK, gate_in(SLOT, &out));
}

/* A slot with room for the trailer and not one byte more is past the bounds:
 * the trailer is checked, and only the missing envelope refuses it. */
static void test_a_slot_of_exactly_the_trailer_reaches_the_trailer_checks(void)
{
    tfw_boot_trailer_t tr, out;

    good_trailer(&tr);
    tr.measurement[0] ^= 0x01u;
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate_in(ESP_LEN + TFW_BOOT_TRAILER_LEN, &out));
    good_trailer(&tr);
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate_in(ESP_LEN + TFW_BOOT_TRAILER_LEN, &out));
    TEST_ASSERT_EQUAL_UINT32(3u, out.image_version);
}

/* One byte after the trailer is no room for the envelope's length, though a
 * whole envelope lies in flash past the slot's end. */
static void test_one_byte_after_the_trailer_is_no_envelope(void)
{
    tfw_boot_trailer_t out;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate_in(ESP_LEN + TFW_BOOT_TRAILER_LEN + 1u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          gate_in(ESP_LEN + TFW_BOOT_TRAILER_LEN + 2u + env_len - 1u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_OK, gate_in(ESP_LEN + TFW_BOOT_TRAILER_LEN + 2u + env_len, &out));
}

/* Signed, for another board: the binding refuses it, though the envelope is
 * good. */
static void test_a_signed_trailer_for_another_board_is_refused(void)
{
    tfw_boot_trailer_t tr, out;

    good_trailer(&tr);
    tr.board_id = (uint8_t)(TFW_BOARD_ID + 1u);
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate_in(SLOT, &out));
}

/* Signed, naming another measurement in its first or its last byte, beside an
 * envelope over the image's real digest: the trailer refuses it. */
static void test_a_signed_trailer_with_another_measurement_is_refused(void)
{
    tfw_boot_trailer_t tr, out;

    good_trailer(&tr);
    tr.measurement[31] ^= 0x80u;
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate_in(SLOT, &out));
    good_trailer(&tr);
    tr.measurement[0] ^= 0x80u;
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate_in(SLOT, &out));
}

/* The SUIT gate repeats the board and the measurement, with the same
 * statuses; the trailer's own checks come first, before the key's pin is
 * read. With no pin for the key, each is still the refusal named. */
static void unpin(void)
{
    tfw_boot_platform_t plat;

    memset(&plat, 0, sizeof plat);
    tfw_boot_platform_serialise(&plat, &tfw_boot_mock_nvm()[TFW_BOOT_EE_BASE]);
}

static void test_the_board_is_refused_before_the_pin_is_read(void)
{
    tfw_boot_trailer_t tr, out;

    unpin();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, gate_in(SLOT, &out));
    good_trailer(&tr);
    tr.board_id = (uint8_t)(TFW_BOARD_ID + 1u);
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate_in(SLOT, &out));
}

static void test_the_last_measurement_byte_is_refused_before_the_pin_is_read(void)
{
    tfw_boot_trailer_t tr, out;

    unpin();
    good_trailer(&tr);
    tr.measurement[31] ^= 0x80u;
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate_in(SLOT, &out));
}

/* A flag this boot does not know: the SUIT gate does not read the flags, so
 * only the binding stands between it and a pass. */
static void test_a_signed_trailer_with_an_unknown_flag_is_refused(void)
{
    tfw_boot_trailer_t tr, out;
    uint8_t            bit = 1u;

    while ((bit & (uint8_t)TFW_BOOT_IMAGE_FLAGS_KNOWN) != 0u) {
        bit = (uint8_t)(bit << 1);
    }
    TEST_ASSERT_NOT_EQUAL(0u, bit);
    good_trailer(&tr);
    tr.image_flags = bit;
    put_trailer(&tr);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate_in(SLOT, &out));
}

/* An EEPROM that does not read is its own failure, not a parse of nothing. */
static void test_an_eeprom_read_failure_is_returned(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_fail_nvm_reads(1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_INTERNAL, gate_in(SLOT, &out));
}

static void test_an_envelope_length_of_zero_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_env_len(0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate_in(SLOT, &out));
}

/* The envelope buffer's own size is a length the gate reads and hands on;
 * one more is refused before a byte is read into it. */
static void test_an_envelope_length_past_the_buffer_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_env_len((uint16_t)TFW_SUIT_ENVELOPE_MAX);
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_RANGE, gate_in(SLOT, &out));
    put_env_len((uint16_t)(TFW_SUIT_ENVELOPE_MAX + 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate_in(SLOT, &out));
    put_env_len(0xFFFFu);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate_in(SLOT + 0x10000u, &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_fixture_passes);
    RUN_TEST(test_a_slot_of_exactly_the_trailer_reaches_the_trailer_checks);
    RUN_TEST(test_one_byte_after_the_trailer_is_no_envelope);
    RUN_TEST(test_a_signed_trailer_for_another_board_is_refused);
    RUN_TEST(test_a_signed_trailer_with_another_measurement_is_refused);
    RUN_TEST(test_the_board_is_refused_before_the_pin_is_read);
    RUN_TEST(test_the_last_measurement_byte_is_refused_before_the_pin_is_read);
    RUN_TEST(test_a_signed_trailer_with_an_unknown_flag_is_refused);
    RUN_TEST(test_an_eeprom_read_failure_is_returned);
    RUN_TEST(test_an_envelope_length_of_zero_is_refused);
    RUN_TEST(test_an_envelope_length_past_the_buffer_is_refused);
    return UNITY_END();
}
