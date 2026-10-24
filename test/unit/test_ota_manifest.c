/*
 * OTA_ACTIVATE's manifest check on AVR (owner ruling 121): the stager, the
 * SUIT reader and OTA_ACTIVATE linked as a 2560 element image links them, under
 * the 2560's profile, whose class id testdata/suit_vectors.h carries. The
 * stream is the image, the envelope after it ([u16 LE length][envelope]), the
 * erased padding and the trailer. Every refusal leaves the store unmarked.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "ota_ctl.h"
#include "ota_auth.h"
#include "boot_platform.h"
#include "dispatch.h"
#include "tfw/protocol.h"
#include "suit_vectors.h"

#define TOTAL      512u
#define TRAILER    128u
#define CHUNK      60u      /* TFW_MAX_PAYLOAD less the offset */
#define SIG_OFFSET 57u      /* test_suit.c's: the envelope's signature */

static uint8_t img[TOTAL];

/* No dispatcher in this link: OTA_ACTIVATE is called right after an OTA_AUTH
 * that held, which is what these two answer, and nothing resets. */
uint8_t tfw_dispatch_last(void)    { return TFW_CMD_OTA_AUTH; }
uint8_t tfw_dispatch_last_ok(void) { return 1u; }
void    tfw_sys_reset_soon(void)   { }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The image, then the envelope after it, then the trailer at the end. */
static void build(const uint8_t *image, uint32_t image_len, const uint8_t *digest,
                  const uint8_t *env, uint16_t env_len, uint32_t version)
{
    uint8_t *t = &img[TOTAL - TRAILER];

    memset(img, 0xFF, sizeof img);
    memcpy(img, image, image_len);
    if (env != NULL) {
        img[image_len]      = (uint8_t)env_len;
        img[image_len + 1u] = (uint8_t)(env_len >> 8);
        memcpy(&img[image_len + 2u], env, env_len);
    }
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = (uint8_t)TFW_BOARD_ID;
    put32(&t[8], version);
    put32(&t[12], image_len);
    memcpy(&t[16], digest, 32u);
}

static tfw_status_t call(tfw_status_t (*fn)(tfw_reader_t *, tfw_writer_t *),
                         const uint8_t *in, uint8_t n)
{
    uint8_t      out[44];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, sizeof out);
    return fn(&r, &w);
}

static void stage(void)
{
    uint8_t  in[4u + CHUNK];
    uint32_t off;
    uint8_t  n;

    put32(in, TOTAL);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_begin, in, 4u));
    for (off = 0u; off < TOTAL; off += CHUNK) {
        n = (uint8_t)(TOTAL - off < CHUNK ? TOTAL - off : CHUNK);
        put32(in, off);
        memcpy(&in[4], &img[off], n);
        TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_data, in, (uint8_t)(4u + n)));
    }
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_end, NULL, 0u));
}

/* OTA_AUTH, then OTA_ACTIVATE in mode 0. */
static tfw_status_t activate(void)
{
    uint8_t sig[TFW_OTA_AUTH_SIG_LEN];
    uint8_t m0 = 0u;

    memset(sig, 0xA5, sizeof sig);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_ota_auth_cmd, sig, (uint8_t)sizeof sig));
    return call(tfw_otactl_cmd_activate, &m0, 1u);
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

static void platform(uint32_t floor)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = floor;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

void setUp(void)
{
    tfw_nor_model_reset();
    tfw_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, SUIT_PUB);
    platform(1u);
}
void tearDown(void) { }

static void test_a_good_manifest_activates(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_OK, activate());
    TEST_ASSERT_TRUE(marked());
}

/* Erased padding reads as length 0xFFFF: no envelope is a refusal. */
static void test_an_image_without_an_envelope_is_refused(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST, NULL, 0u, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, activate());
    TEST_ASSERT_FALSE(marked());
}

/* A length of zero, and one past the padding before the trailer. */
static void test_a_length_the_padding_cannot_hold_is_refused(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    img[sizeof SUIT_IMAGE]      = 0x00u;
    img[sizeof SUIT_IMAGE + 1u] = 0x00u;
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, activate());
    TEST_ASSERT_FALSE(marked());

    /* 512 - 128 - 64 = 320 bytes of padding: 2 + 319 does not fit. */
    img[sizeof SUIT_IMAGE]      = 0x3Fu;
    img[sizeof SUIT_IMAGE + 1u] = 0x01u;
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, activate());
    TEST_ASSERT_FALSE(marked());
}

static void test_a_flipped_signature_byte_is_refused(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    img[sizeof SUIT_IMAGE + 2u + SIG_OFFSET + 5u] ^= 0x01u;
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, activate());
    TEST_ASSERT_FALSE(marked());
}

static void test_another_boards_class_id_is_refused(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE_CLASS_328P, sizeof SUIT_ENVELOPE_CLASS_328P, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, activate());
    TEST_ASSERT_FALSE(marked());
}

/* A staged image the trailer measures but the manifest does not name. */
static void test_an_image_the_manifest_does_not_name_is_refused(void)
{
    build(SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY, SUIT_POLICY_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, activate());
    TEST_ASSERT_FALSE(marked());
}

static void test_a_sequence_that_is_not_the_trailers_version_is_refused(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 8u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, activate());
    TEST_ASSERT_FALSE(marked());
}

/* The manifest's sequence is 7: a floor of 8 is a rollback refused at
 * OTA_ACTIVATE, before the MARK, not first by tfw-boot at the next reset. */
static void test_a_sequence_below_the_floor_is_refused(void)
{
    platform(8u);
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, activate());
    TEST_ASSERT_FALSE(marked());
}

static void test_a_sequence_at_the_floor_activates(void)
{
    platform(7u);
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_OK, activate());
    TEST_ASSERT_TRUE(marked());
}

/* A refusal spends the authorization; a fresh OTA_AUTH and the good image
 * restaged still activate. */
static void test_a_refusal_does_not_wedge_the_session(void)
{
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 8u);
    stage();
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, activate());
    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    TEST_ASSERT_EQUAL(TFW_OK, activate());
    TEST_ASSERT_TRUE(marked());
}

/* The checks below call tfw_ota_manifest directly: OTA_ACTIVATE's own
 * refusals come first there and would mask the ones under test. */
static tfw_status_t manifest(void)
{
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.boot_state = TFW_BOOT_STATE_GOOD;
    p.ota_state  = TFW_BOOT_OTA_IDLE;
    return tfw_ota_manifest(&p);
}

/* A stage begun but never ended has no session for the check to read. */
static void test_a_stage_that_never_ended_is_refused_before_the_store_is_read(void)
{
    uint8_t in[4];

    put32(in, TOTAL);
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_stage_cmd_begin, in, 4u));
    tfw_nor_model_fail_xfer(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, manifest());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_nor_model_take_fault());   /* still armed: nothing read */
    TEST_ASSERT_FALSE(marked());
}

/* The trailer OTA_END checked, flipped in the store behind the driver's
 * back before OTA_ACTIVATE reads it again. */
static void test_a_trailer_changed_after_ota_end_is_refused(void)
{
    uint32_t programs;

    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    tfw_nor_model_corrupt(TFW_OTA_STORE_STAGE_ADDR + TOTAL - TRAILER);
    programs = tfw_nor_model_programs();
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, manifest());
    TEST_ASSERT_EQUAL_UINT32(programs, tfw_nor_model_programs());
    TEST_ASSERT_FALSE(marked());
}

/* An image that leaves one byte of padding before the trailer has no room
 * for even the envelope's length. */
static void test_an_image_that_leaves_no_room_for_the_length_is_refused(void)
{
    static uint8_t image[TOTAL - TRAILER - 1u];
    uint8_t        digest[TFW_SHA256_LEN];
    uint32_t       programs;

    memset(image, 0x5A, sizeof image);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(image, (uint16_t)sizeof image, digest));
    build(image, sizeof image, digest, NULL, 0u, 7u);
    stage();
    programs = tfw_nor_model_programs();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, manifest());
    TEST_ASSERT_EQUAL_UINT32(programs, tfw_nor_model_programs());
    TEST_ASSERT_FALSE(marked());
}

/* Every store transfer the check makes -- the trailer, the envelope's
 * length, the envelope -- failed in turn: each failure is the status
 * returned, and none marks. The first n past the last transfer passes. */
static void test_every_failed_store_read_is_returned_and_marks_nothing(void)
{
    tfw_status_t st;
    uint8_t      n;

    build(SUIT_IMAGE, sizeof SUIT_IMAGE, SUIT_IMAGE_DIGEST,
          SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, 7u);
    stage();
    for (n = 1u; n < 255u; n++) {
        tfw_nor_model_fail_xfer(n);
        st = manifest();
        tfw_nor_model_fail_xfer(0u);
        if (st == TFW_OK) {
            break;
        }
        TEST_ASSERT_EQUAL(TFW_ERR_BUSY, st);
        TEST_ASSERT_FALSE(marked());
    }
    TEST_ASSERT_EQUAL(TFW_OK, st);
    TEST_ASSERT_GREATER_THAN_UINT8(3u, n);   /* trailer, length, envelope */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_manifest_activates);
    RUN_TEST(test_an_image_without_an_envelope_is_refused);
    RUN_TEST(test_a_length_the_padding_cannot_hold_is_refused);
    RUN_TEST(test_a_flipped_signature_byte_is_refused);
    RUN_TEST(test_another_boards_class_id_is_refused);
    RUN_TEST(test_an_image_the_manifest_does_not_name_is_refused);
    RUN_TEST(test_a_sequence_that_is_not_the_trailers_version_is_refused);
    RUN_TEST(test_a_sequence_below_the_floor_is_refused);
    RUN_TEST(test_a_sequence_at_the_floor_activates);
    RUN_TEST(test_a_refusal_does_not_wedge_the_session);
    RUN_TEST(test_a_stage_that_never_ended_is_refused_before_the_store_is_read);
    RUN_TEST(test_a_trailer_changed_after_ota_end_is_refused);
    RUN_TEST(test_an_image_that_leaves_no_room_for_the_length_is_refused);
    RUN_TEST(test_every_failed_store_read_is_returned_and_marks_nothing);
    return UNITY_END();
}
