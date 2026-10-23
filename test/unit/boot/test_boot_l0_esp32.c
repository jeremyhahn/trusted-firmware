/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 layer 0's ES256 gate (boot/hal/esp32/l0_gate.c) against the mock
 * boot HAL: an image laid out as ESP-IDF lays it out -- the image, its
 * appended SHA-256, then trailer v1, then the SUIT envelope behind its u16
 * length -- and the pin in the platform block. esp_image_verify is not here:
 * the gate runs after it and is handed what it returned, so the mock of
 * ESP-IDF is the (base, length, digest) it reports. Every refusal, and the
 * accept. Built three times (mk/boot.mk) against the esp32-qemu profile, whose
 * class id the manifest names: with p256-soft, as the C3 and the ESP32 link
 * it; with the ROM backend the P4 links; and with p256-soft over the
 * bootloader SHA the C3 and the P4 hash through (measure_esp32.c).
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

#define BASE      0x0400u                 /* the slot's flash offset */
#define SLOT      0x2000u
#define IMAGE     1000u                   /* not a multiple of anything */
#define ESP_LEN   (IMAGE + 32u)           /* up to and including the digest */
#define ENV_AT    (BASE + ESP_LEN + TFW_BOOT_TRAILER_LEN)  /* the u16, then the envelope */

static const uint8_t image_key[32] = {
    0x5A,0x13,0xC7,0x2E,0x91,0x0B,0x64,0xF2,0x38,0xAD,0x7E,0x46,0x09,0xD5,0x6C,0x81,
    0x22,0xB9,0x4F,0xE0,0x17,0x8A,0x3D,0x56,0xCB,0x70,0x05,0x9E,0x63,0xF4,0x28,0xB1
};
static const uint8_t other_key[32] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0x0F,0x10,
    0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98,0xA9,0xBA,0xCB,0xDC,0xED,0xFE,0x1F,0x20
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

/* ES256 over a digest, r || s big-endian, deterministic so a run is a run. */
static void sign(const uint8_t key[32], const uint8_t h[32], uint8_t sig[64])
{
    /* RFC 6979 makes a signature a function of the key and the hash, so one
     * made once is the one every later case would make: setUp signs the same
     * trailer and manifest before every case, and on the boot's 8-bit word
     * path each signature costs seconds under the sanitizers (#163). */
    static struct { uint8_t key[32]; uint8_t h[32]; uint8_t sig[64]; } memo[16];
    static unsigned memo_n;
    uint8_t    tmp[32 + 32 + 64];
    hash_ctx_t hc;
    unsigned   i;

    for (i = 0u; i < memo_n; i++) {
        if (memcmp(memo[i].key, key, 32u) == 0 && memcmp(memo[i].h, h, 32u) == 0) {
            memcpy(sig, memo[i].sig, 64u);
            return;
        }
    }
    memset(&hc, 0, sizeof hc);
    hc.uecc.init_hash   = h_init;
    hc.uecc.update_hash = h_update;
    hc.uecc.finish_hash = h_finish;
    hc.uecc.block_size  = 64u;
    hc.uecc.result_size = 32u;
    hc.uecc.tmp         = tmp;
    TEST_ASSERT_EQUAL_INT(1, uECC_sign_deterministic(key, h, 32u, &hc.uecc, sig,
                                                     uECC_secp256r1()));
    if (memo_n < sizeof memo / sizeof memo[0]) {
        memcpy(memo[memo_n].key, key, 32u);
        memcpy(memo[memo_n].h, h, 32u);
        memcpy(memo[memo_n].sig, sig, 64u);
        memo_n++;
    }
}

/* A trailer at base + ESP_LEN, signed with `key`. */
static void put_trailer(const tfw_boot_trailer_t *tr, const uint8_t key[32])
{
    uint8_t *raw = &tfw_boot_mock_flash()[BASE + ESP_LEN];
    uint8_t  h[32];

    tfw_boot_trailer_serialise(tr, raw);
    tfw_sha256(raw, TFW_BOOT_TR_SIGNED_LEN, h);
    sign(key, h, &raw[TFW_BOOT_TR_SIGNATURE]);
}

static const uint8_t vendor_id[16] = TFW_SUIT_VENDOR_ID;
static const uint8_t class_id[16]  = TFW_SUIT_CLASS_ID;
static uint16_t      env_len;

/* What l0_seal.py appends after the trailer: a u16 little-endian length, then
 * the envelope suit-emit builds -- here through its encoder, emit.c -- with
 * its Sig_structure signed by `key`. `flip` flips a bit of the signature. */
static void put_envelope(uint32_t seq, const uint8_t image_digest[32], uint32_t size,
                         const uint8_t cls[16], const uint8_t key[32], uint8_t flip)
{
    uint8_t *at = &tfw_boot_mock_flash()[ENV_AT];
    uint8_t  man[160];
    uint8_t  tbs[96];
    uint8_t  h[32];
    uint8_t  sig[64];
    size_t   mlen;
    size_t   tlen;

    mlen = tfw_suit_emit_manifest(man, sizeof man, image_digest, size, seq, vendor_id, cls);
    TEST_ASSERT_NOT_EQUAL(0u, mlen);
    tlen = tfw_suit_emit_tbs(tbs, sizeof tbs, man, mlen);
    TEST_ASSERT_NOT_EQUAL(0u, tlen);
    tfw_sha256(tbs, (uint16_t)tlen, h);
    sign(key, h, sig);
    sig[9] ^= flip;
    env_len = (uint16_t)tfw_suit_emit_envelope(&at[2], TFW_SUIT_ENVELOPE_MAX, man, mlen, sig);
    TEST_ASSERT_NOT_EQUAL(0u, env_len);
    at[0] = (uint8_t)env_len;
    at[1] = (uint8_t)(env_len >> 8);
}

static void good_trailer(tfw_boot_trailer_t *tr)
{
    memset(tr, 0, sizeof *tr);
    tr->image_version = 3u;
    tr->image_len     = IMAGE;
    tr->board_id      = (uint8_t)TFW_BOARD_ID;
    memcpy(tr->measurement, digest, 32u);
}

/* A platform block, serialised as tfwprov bakes it, with this pin. */
static void pin(const uint8_t hash[32])
{
    tfw_boot_platform_t plat;

    memset(&plat, 0, sizeof plat);
    memcpy(plat.pubkey_hash, hash, 32u);
    tfw_boot_platform_serialise(&plat, &tfw_boot_mock_nvm()[TFW_BOOT_EE_BASE]);
}

/* The slot's key, and its hash pinned in the platform block. */
static void install_key(const uint8_t key[32])
{
    /* The public key derived once per private key, as sign() keeps its
     * signatures: setUp installs the same key before every case. */
    static struct { uint8_t key[32]; uint8_t pub[64]; } memo[4];
    static unsigned memo_n;
    uint8_t  hash[32];
    unsigned i;

    for (i = 0u; i < memo_n && memcmp(memo[i].key, key, 32u) != 0; i++) {
    }
    if (i < memo_n) {
        memcpy(tfw_boot_p256_key, memo[i].pub, 64u);
    } else {
        TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(key, tfw_boot_p256_key, uECC_secp256r1()));
        if (memo_n < sizeof memo / sizeof memo[0]) {
            memcpy(memo[memo_n].key, key, 32u);
            memcpy(memo[memo_n].pub, tfw_boot_p256_key, 64u);
            memo_n++;
        }
    }
    tfw_sha256(tfw_boot_p256_key, 64u, hash);
    pin(hash);
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
    memcpy(&f[BASE + IMAGE], digest, 32u);          /* ESP-IDF's appended SHA-256 */
    install_key(image_key);
    good_trailer(&tr);
    put_trailer(&tr, image_key);
    put_envelope(3u, digest, IMAGE, class_id, image_key, 0u);
}

void tearDown(void) {}

static tfw_status_t gate(uint32_t floor, tfw_boot_trailer_t *out)
{
    return tfw_l0_gate(BASE, SLOT, ESP_LEN, digest, floor, out);
}

static void test_a_good_image_passes_and_returns_its_trailer(void)
{
    tfw_boot_trailer_t out;

    TEST_ASSERT_EQUAL_INT(TFW_OK, gate(3u, &out));
    TEST_ASSERT_EQUAL_UINT32(3u, out.image_version);
    TEST_ASSERT_EQUAL_UINT32(IMAGE, out.image_len);
    TEST_ASSERT_EQUAL_MEMORY(digest, out.measurement, 32u);
}

static void test_no_trailer_is_refused(void)
{
    tfw_boot_trailer_t out;

    memset(&tfw_boot_mock_flash()[BASE + ESP_LEN], 0xFF, TFW_BOOT_TRAILER_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate(0u, &out));
}

static void test_a_bad_magic_is_refused(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_flash()[BASE + ESP_LEN] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate(0u, &out));
}

static void test_version_zero_is_refused(void)
{
    tfw_boot_trailer_t tr;
    tfw_boot_trailer_t out;

    good_trailer(&tr);
    tr.image_version = 0u;
    put_trailer(&tr, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_another_boards_image_is_refused(void)
{
    tfw_boot_trailer_t tr;
    tfw_boot_trailer_t out;

    good_trailer(&tr);
    tr.board_id = (uint8_t)(TFW_BOARD_ID + 1u);
    put_trailer(&tr, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate(0u, &out));
}

static void test_an_unbound_image_is_refused(void)
{
    tfw_boot_trailer_t tr;
    tfw_boot_trailer_t out;

    good_trailer(&tr);
    tr.board_id = 0u;
    put_trailer(&tr, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate(0u, &out));
}

static void test_an_image_below_the_floor_is_refused(void)
{
    tfw_boot_trailer_t out;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, gate(4u, &out));
}

static void test_a_measurement_that_is_not_the_images_is_refused(void)
{
    tfw_boot_trailer_t out;

    digest[31] ^= 0x80u;                /* what esp_image_verify reported */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_trailer_naming_another_length_is_refused(void)
{
    tfw_boot_trailer_t tr;
    tfw_boot_trailer_t out;

    good_trailer(&tr);
    tr.image_len = IMAGE + 32u;         /* the span with the digest in it */
    put_trailer(&tr, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_key_the_device_was_not_paired_with_is_refused(void)
{
    tfw_boot_trailer_t out;

    uint8_t hash[32];

    tfw_sha256(tfw_boot_p256_key, 64u, hash);
    hash[5] ^= 0x01u;
    pin(hash);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, gate(0u, &out));
}

static void test_an_unprovisioned_pin_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* An erased tfw_plat: no block, so no pin. */
    memset(&tfw_boot_mock_nvm()[TFW_BOOT_EE_BASE], 0xFF, TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED, gate(0u, &out));
}

static void test_a_corrupt_platform_block_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* The pin bit flipped in place, the CRC left: the block is not believed,
     * so a pin rewritten without its CRC never reaches the compare. */
    tfw_boot_mock_nvm()[TFW_BOOT_EE_BASE + TFW_BOOT_EE_O_PUBKEY_HASH + 5u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, gate(0u, &out));
}

static void test_a_failed_pin_read_is_refused(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_fail_nvm_reads(1u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, gate(0u, &out));
}

static void test_a_bad_signature_is_refused(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_flash()[BASE + ESP_LEN + TFW_BOOT_TR_SIGNATURE + 9u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_trailer_signed_by_another_key_is_refused(void)
{
    tfw_boot_trailer_t tr;
    tfw_boot_trailer_t out;

    good_trailer(&tr);
    put_trailer(&tr, other_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_another_key_pinned_with_it_is_still_refused(void)
{
    tfw_boot_trailer_t out;

    /* A bootloader with another key AND a pin rewritten to match it passes
     * the pin; the image is still signed by the first key. */
    install_key(other_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_signed_field_flipped_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* image_version's high byte: parses, permits, measures -- and the
     * signature no longer covers it. */
    tfw_boot_mock_flash()[BASE + ESP_LEN + TFW_BOOT_TR_IMAGE_VERSION + 3u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_bit_flipped_image_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint8_t           *f = tfw_boot_mock_flash();

    /* One bit of the image, and the digest ESP-IDF appends and reports
     * recomputed over it: esp_image_verify passes such an image, the gate
     * does not. */
    f[BASE + 500u] ^= 0x10u;
    tfw_sha256(&f[BASE], IMAGE, digest);
    memcpy(&f[BASE + IMAGE], digest, 32u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_an_erased_key_slot_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint8_t            hash[32];

    /* The slot as it ships, its hash pinned: the pin passes, the verify
     * refuses. */
    memset(tfw_boot_p256_key, 0xFF, sizeof tfw_boot_p256_key);
    tfw_sha256(tfw_boot_p256_key, 64u, hash);
    pin(hash);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_trailer_past_the_slot_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint32_t           fit = ESP_LEN + TFW_BOOT_TRAILER_LEN + 2u + env_len;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_l0_gate(BASE, ESP_LEN + TFW_BOOT_TRAILER_LEN - 1u, ESP_LEN,
                                      digest, 0u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_l0_gate(BASE, SLOT, SLOT + 1u, digest, 0u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_l0_gate(BASE, SLOT, 32u, digest, 0u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_l0_gate(BASE, fit, ESP_LEN, digest, 0u, &out));
}

/* The SUIT manifest after the trailer (design section 2.2). Each refusal
 * comes after a trailer that passes on its own. */

static void test_an_envelope_past_the_slot_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint32_t           fit = ESP_LEN + TFW_BOOT_TRAILER_LEN + 2u + env_len;

    /* The trailer fits and the envelope does not: the trailer alone, the
     * length alone, and one byte short. */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_l0_gate(BASE, ESP_LEN + TFW_BOOT_TRAILER_LEN, ESP_LEN,
                                      digest, 0u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_l0_gate(BASE, ESP_LEN + TFW_BOOT_TRAILER_LEN + 2u, ESP_LEN,
                                      digest, 0u, &out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_l0_gate(BASE, fit - 1u, ESP_LEN, digest, 0u, &out));
}

static void test_no_envelope_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* Erased after the trailer, as an image sealed without one: 0xFFFF. */
    memset(&tfw_boot_mock_flash()[ENV_AT], 0xFF, 2u + TFW_SUIT_ENVELOPE_MAX);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate(0u, &out));
}

static void test_an_empty_envelope_is_refused(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_flash()[ENV_AT]      = 0u;
    tfw_boot_mock_flash()[ENV_AT + 1u] = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate(0u, &out));
}

static void test_an_envelope_past_its_maximum_is_refused(void)
{
    tfw_boot_trailer_t out;

    tfw_boot_mock_flash()[ENV_AT]      = (uint8_t)(TFW_SUIT_ENVELOPE_MAX + 1u);
    tfw_boot_mock_flash()[ENV_AT + 1u] = (uint8_t)((TFW_SUIT_ENVELOPE_MAX + 1u) >> 8);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate(0u, &out));
}

static void test_a_truncated_envelope_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* The length one short: the reader runs out inside the envelope. */
    tfw_boot_mock_flash()[ENV_AT] = (uint8_t)(env_len - 1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate(0u, &out));
}

static void test_a_manifest_naming_another_digest_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint8_t            other[32];

    memcpy(other, digest, 32u);
    other[0] ^= 0x01u;
    put_envelope(3u, other, IMAGE, class_id, image_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_manifest_naming_another_class_is_refused(void)
{
    tfw_boot_trailer_t out;
    uint8_t            other[16];

    memcpy(other, class_id, 16u);
    other[15] ^= 0x01u;
    put_envelope(3u, digest, IMAGE, other, image_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, gate(0u, &out));
}

static void test_a_manifest_naming_another_size_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_envelope(3u, digest, ESP_LEN, class_id, image_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, gate(0u, &out));
}

static void test_a_manifest_sequence_below_the_floor_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* The trailer is at the floor and passes; the manifest names one below. */
    put_envelope(2u, digest, IMAGE, class_id, image_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, gate(3u, &out));
}

static void test_a_manifest_sequence_past_the_trailer_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_envelope(4u, digest, IMAGE, class_id, image_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, gate(0u, &out));
}

static void test_a_bad_manifest_signature_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_envelope(3u, digest, IMAGE, class_id, image_key, 0x01u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_manifest_signed_by_another_key_is_refused(void)
{
    tfw_boot_trailer_t out;

    put_envelope(3u, digest, IMAGE, class_id, other_key, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

static void test_a_manifest_byte_flipped_is_refused(void)
{
    tfw_boot_trailer_t out;

    /* The last byte, inside the manifest: its digest no longer holds. */
    tfw_boot_mock_flash()[ENV_AT + 2u + env_len - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, gate(0u, &out));
}

/* ---- OTA_ACTIVATE's authorization, read from tfw_auth on a trial boot */

/* A record authorizing (measurement, version) in `mode`, signed with `key`
 * over the Sig_structure; `as_mode` is the mode the record says. */
static void auth_record(uint8_t rec[TFW_BOOT_AUTH_LEN], const uint8_t *measurement,
                        uint32_t version, uint8_t mode, uint8_t as_mode, const uint8_t key[32])
{
    uint8_t msg[TFW_BOOT_COSE_ACTIVATE_MAX];
    uint8_t h[32];

    memset(rec, 0, TFW_BOOT_AUTH_LEN);
    tfw_boot_wr_u32(&rec[TFW_BOOT_AUTH_O_MAGIC], TFW_BOOT_AUTH_MAGIC);
    rec[TFW_BOOT_AUTH_O_MODE] = as_mode;
    tfw_sha256(msg, tfw_boot_cose_activate(msg, measurement, version, mode), h);
    sign(key, h, &rec[TFW_BOOT_AUTH_O_SIG]);
}

static void passed(tfw_boot_trailer_t *tr)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, gate(3u, tr));
}

static void test_an_authorized_slot_passes_in_either_mode(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];
    uint8_t            mode;

    passed(&tr);
    for (mode = 0u; mode <= 1u; mode++) {
        auth_record(rec, digest, 3u, mode, mode, image_key);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_l0_authorized(&tr, rec));
    }
}

static void test_no_record_is_unauthorized(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];

    passed(&tr);
    memset(rec, 0xFF, sizeof rec);                  /* the erased partition */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
    auth_record(rec, digest, 3u, 0u, 0u, image_key);
    rec[TFW_BOOT_AUTH_O_MAGIC] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

static void test_a_mode_past_one_is_unauthorized(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];

    passed(&tr);
    auth_record(rec, digest, 3u, 2u, 2u, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

static void test_a_record_with_another_mode_is_unauthorized(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];

    passed(&tr);
    auth_record(rec, digest, 3u, 0u, 1u, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

/* An authorization is for one image: another's measurement or version, which
 * the slot's own trailer does not carry, does not authorize this one. */
static void test_another_images_authorization_is_unauthorized(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];
    uint8_t            other[32];

    passed(&tr);
    memcpy(other, digest, sizeof other);
    other[0] ^= 0x01u;
    auth_record(rec, other, 3u, 0u, 0u, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
    auth_record(rec, digest, 4u, 0u, 0u, image_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

static void test_an_authorization_by_another_key_is_unauthorized(void)
{
    tfw_boot_trailer_t tr;
    uint8_t            rec[TFW_BOOT_AUTH_LEN];

    passed(&tr);
    auth_record(rec, digest, 3u, 0u, 0u, other_key);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
    auth_record(rec, digest, 3u, 0u, 0u, image_key);
    rec[TFW_BOOT_AUTH_O_SIG + 40u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_image_passes_and_returns_its_trailer);
    RUN_TEST(test_no_trailer_is_refused);
    RUN_TEST(test_a_bad_magic_is_refused);
    RUN_TEST(test_version_zero_is_refused);
    RUN_TEST(test_another_boards_image_is_refused);
    RUN_TEST(test_an_unbound_image_is_refused);
    RUN_TEST(test_an_image_below_the_floor_is_refused);
    RUN_TEST(test_a_measurement_that_is_not_the_images_is_refused);
    RUN_TEST(test_a_trailer_naming_another_length_is_refused);
    RUN_TEST(test_a_key_the_device_was_not_paired_with_is_refused);
    RUN_TEST(test_an_unprovisioned_pin_is_refused);
    RUN_TEST(test_a_corrupt_platform_block_is_refused);
    RUN_TEST(test_a_failed_pin_read_is_refused);
    RUN_TEST(test_a_bad_signature_is_refused);
    RUN_TEST(test_a_trailer_signed_by_another_key_is_refused);
    RUN_TEST(test_another_key_pinned_with_it_is_still_refused);
    RUN_TEST(test_a_signed_field_flipped_is_refused);
    RUN_TEST(test_a_bit_flipped_image_is_refused);
    RUN_TEST(test_an_erased_key_slot_is_refused);
    RUN_TEST(test_a_trailer_past_the_slot_is_refused);
    RUN_TEST(test_an_envelope_past_the_slot_is_refused);
    RUN_TEST(test_no_envelope_is_refused);
    RUN_TEST(test_an_empty_envelope_is_refused);
    RUN_TEST(test_an_envelope_past_its_maximum_is_refused);
    RUN_TEST(test_a_truncated_envelope_is_refused);
    RUN_TEST(test_a_manifest_naming_another_digest_is_refused);
    RUN_TEST(test_a_manifest_naming_another_class_is_refused);
    RUN_TEST(test_a_manifest_naming_another_size_is_refused);
    RUN_TEST(test_a_manifest_sequence_below_the_floor_is_refused);
    RUN_TEST(test_a_manifest_sequence_past_the_trailer_is_refused);
    RUN_TEST(test_a_bad_manifest_signature_is_refused);
    RUN_TEST(test_a_manifest_signed_by_another_key_is_refused);
    RUN_TEST(test_a_manifest_byte_flipped_is_refused);
    RUN_TEST(test_an_authorized_slot_passes_in_either_mode);
    RUN_TEST(test_no_record_is_unauthorized);
    RUN_TEST(test_a_mode_past_one_is_unauthorized);
    RUN_TEST(test_a_record_with_another_mode_is_unauthorized);
    RUN_TEST(test_another_images_authorization_is_unauthorized);
    RUN_TEST(test_an_authorization_by_another_key_is_unauthorized);
    return UNITY_END();
}
