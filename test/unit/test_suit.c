/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT manifest reader and its activation gate (issue #78), against the
 * software element holding the vectors' public key in the image-signing slot
 * -- so every signature here is checked by real ECDSA, the element tier's own
 * path, and not by a flag.
 *
 * Built with the ATmega2560's profile (mk/host.mk): the 2560 is the board that
 * links the reader, and the vectors carry its class id.
 *
 * Every refusal is held to the same three facts: the gate's status, an OTA
 * state of FAILED, and tfw_boot_trial_activate then refusing, so nothing is
 * PENDING and no boot request has been written.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "boot_trial.h"
#include "suit.h"
#include "suit_vectors.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static tfw_boot_platform_t plat;
static tfw_boot_trailer_t  tr;
static uint8_t             env[TFW_SUIT_ENVELOPE_MAX + 8u];

/* Where the signature sits in SUIT_ENVELOPE: tag, map, key, the auth bstr's
 * two-byte head, the array head, the 38-byte manifest digest, the COSE bstr's
 * head, then eight bytes of COSE before the signature's own two-byte head. */
#define SIG_OFFSET 57u

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, SUIT_PUB);

    memset(&plat, 0, sizeof plat);
    plat.ota_state  = TFW_BOOT_OTA_RECEIVING;
    plat.boot_state = TFW_BOOT_STATE_GOOD;

    memset(&tr, 0, sizeof tr);
    tr.image_version = 7u;
    tr.image_len     = sizeof SUIT_IMAGE;
    tr.board_id      = TFW_BOARD_ID;
    memcpy(tr.measurement, SUIT_IMAGE_DIGEST, sizeof tr.measurement);

    memcpy(env, SUIT_ENVELOPE, sizeof SUIT_ENVELOPE);
}

void tearDown(void) {}

static tfw_status_t gate(const uint8_t *e, uint16_t len, const uint8_t *digest, uint32_t size)
{
    return tfw_suit_gate(&plat, &tr, digest, size, e, len);
}

/* A refusal leaves nothing to activate and no request written. */
static void assert_refused_before_activation(void)
{
    static const uint8_t zero[TFW_BOOT_HANDOFF_LEN] = { 0u };

    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE,
                      tfw_boot_trial_activate(&plat, SUIT_IMAGE_DIGEST, NULL));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    TEST_ASSERT_EQUAL_MEMORY(zero, test_handoff, sizeof zero);
}

static void test_the_vectors_are_what_the_signature_offset_says(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x58u, SUIT_ENVELOPE[SIG_OFFSET - 2u]);
    TEST_ASSERT_EQUAL_HEX8(0x40u, SUIT_ENVELOPE[SIG_OFFSET - 1u]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, SUIT_ENVELOPE[SIG_OFFSET + TFW_SIG_LEN]);
}

static void test_a_valid_envelope_reads(void)
{
    tfw_suit_manifest_t m;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE, sizeof SUIT_ENVELOPE, &m));
    TEST_ASSERT_EQUAL_UINT32(7u, m.seq);
    TEST_ASSERT_EQUAL_UINT32(sizeof SUIT_IMAGE, m.image_size);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_IMAGE_DIGEST, m.image_digest, sizeof m.image_digest);
}

static void test_a_valid_envelope_passes_the_gate_and_activates(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                   sizeof SUIT_IMAGE));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_VERIFIED, plat.ota_state);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_activate(&plat, SUIT_IMAGE_DIGEST, NULL));
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_PENDING, plat.ota_state);
}

static void test_a_flipped_signature_byte_is_refused(void)
{
    env[SIG_OFFSET + 5u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                           sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_tampered_manifest_byte_is_refused(void)
{
    env[sizeof SUIT_ENVELOPE - 1u] ^= 0x01u;     /* the image size, inside the manifest */
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                           sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_wrong_image_digest_is_refused(void)
{
    uint8_t staged[TFW_SUIT_DIGEST_LEN];

    memcpy(staged, SUIT_IMAGE_DIGEST, sizeof staged);
    staged[0] ^= 0x80u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, gate(env, sizeof SUIT_ENVELOPE, staged,
                                           sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_trailer_measurement_that_disagrees_is_refused(void)
{
    tr.measurement[31] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                           sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_wrong_size_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                          sizeof SUIT_IMAGE - 1u));
    assert_refused_before_activation();
}

static void test_a_wrong_class_id_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                      gate(SUIT_ENVELOPE_CLASS_328P, sizeof SUIT_ENVELOPE_CLASS_328P,
                           SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_trailer_for_another_board_is_refused(void)
{
    tr.board_id = (uint8_t)(TFW_BOARD_ID + 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, gate(env, sizeof SUIT_ENVELOPE,
                                                SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_sequence_that_is_not_the_trailers_version_is_refused(void)
{
    tr.image_version = 8u;
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, gate(env, sizeof SUIT_ENVELOPE,
                                                     SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_sequence_below_the_floor_is_refused(void)
{
    plat.rollback_floor = 8u;
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, gate(env, sizeof SUIT_ENVELOPE,
                                                     SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_sequence_at_the_floor_passes(void)
{
    plat.rollback_floor = 7u;
    TEST_ASSERT_EQUAL(TFW_OK, gate(env, sizeof SUIT_ENVELOPE,
                                   SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    TEST_ASSERT_EQUAL(TFW_BOOT_OTA_VERIFIED, plat.ota_state);
}

static void test_an_unknown_member_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED,
                      gate(SUIT_ENVELOPE_UNKNOWN, sizeof SUIT_ENVELOPE_UNKNOWN,
                           SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_trailing_byte_is_refused(void)
{
    env[sizeof SUIT_ENVELOPE] = 0x00u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, gate(env, sizeof SUIT_ENVELOPE + 1u,
                                                SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_a_truncated_envelope_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, sizeof SUIT_ENVELOPE - 1u,
                                          SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

/* Every prefix, not just the one byte short: a reader that trusted any length
 * would accept or overrun somewhere in here. */
static void test_every_prefix_is_refused(void)
{
    tfw_suit_manifest_t m;
    uint16_t            n;

    for (n = 0u; n < sizeof SUIT_ENVELOPE; n++) {
        TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_suit_read(SUIT_ENVELOPE, n, &m));
    }
}

static void test_an_oversized_length_is_refused(void)
{
    env[5] = 0xFFu;                               /* the auth wrapper's length */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, sizeof SUIT_ENVELOPE,
                                          SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_an_eight_byte_length_is_refused(void)
{
    env[4] = 0x5Bu;                               /* bstr, 8-byte length */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, sizeof SUIT_ENVELOPE,
                                          SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_an_envelope_past_the_bound_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, TFW_SUIT_ENVELOPE_MAX + 1u,
                                          SUIT_IMAGE_DIGEST, sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

static void test_without_the_key_nothing_verifies(void)
{
    tfw_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_verify_result(0u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                       sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

/* suit.c cose_sign1, the kid branch: an unprotected header of {4: h'01'} is
 * read past. The header is outside what the signature covers, so the vector's
 * signature still verifies once the two enclosing lengths grow by three. */
static void test_a_kid_in_the_unprotected_header_reads(void)
{
    static const uint8_t kid_map[4] = { 0xA1u, 0x04u, 0x41u, 0x01u };   /* {4: h'01'} */
    tfw_suit_manifest_t  m;
    uint16_t             n = (uint16_t)(sizeof SUIT_ENVELOPE + 3u);

    TEST_ASSERT_TRUE(n <= TFW_SUIT_ENVELOPE_MAX);
    TEST_ASSERT_EQUAL_HEX8(0xA0u, SUIT_ENVELOPE[53]);   /* the empty map it replaces */
    memcpy(env, SUIT_ENVELOPE, 53u);
    memcpy(&env[53], kid_map, sizeof kid_map);
    memcpy(&env[57], &SUIT_ENVELOPE[54], sizeof SUIT_ENVELOPE - 54u);
    env[5]  = (uint8_t)(SUIT_ENVELOPE[5] + 3u);    /* the authentication wrapper */
    env[46] = (uint8_t)(SUIT_ENVELOPE[46] + 3u);   /* the COSE_Sign1 */

    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(env, n, &m));
    TEST_ASSERT_EQUAL_UINT32(7u, m.seq);
}

/* Re-digest the manifest at env[122..221] and re-sign that digest with a key
 * the test holds, so a manifest edit reaches the manifest reader. */
static void resign(void)
{
    static const uint8_t prefix[19] = {
        0x84u, 0x6Au, 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', '1',
        0x43u, 0xA1u, 0x01u, 0x26u, 0x40u, 0x58u, 0x24u
    };
    uint8_t priv[32];
    uint8_t pub[64];
    uint8_t tbs[sizeof prefix + 36u];
    uint8_t h[32];
    uint8_t i;

    for (i = 0u; i < sizeof priv; i++) {
        priv[i] = (uint8_t)(i + 1u);
    }
    TEST_ASSERT_EQUAL_INT(1, tfw_model_p256_public(priv, pub));
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, pub);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(&env[122], (uint16_t)(sizeof SUIT_ENVELOPE - 122u), &env[13]));
    memcpy(tbs, prefix, sizeof prefix);
    memcpy(&tbs[sizeof prefix], &env[9], 36u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(tbs, sizeof tbs, h));
    TEST_ASSERT_EQUAL_INT(1, tfw_model_p256_sign(priv, h, &env[57]));
}

/* suit.c common, the components branch: a component identifier that is a
 * bstr where an array belongs is UNSUPPORTED. */
static void test_a_component_id_that_is_not_an_array_is_refused(void)
{
    tfw_suit_manifest_t m;

    TEST_ASSERT_EQUAL_HEX8(0x81u, env[135]);   /* [[h'00']]: the inner array */
    env[135] = 0x41u;
    resign();
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_suit_read(env, sizeof SUIT_ENVELOPE, &m));
}

/* The re-signing itself, so the refusal above is the manifest's and not the
 * signature's. */
static void test_a_resigned_envelope_reads(void)
{
    tfw_suit_manifest_t m;

    resign();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(env, sizeof SUIT_ENVELOPE, &m));
}

/* One field inside the signed manifest, rewritten in place and re-signed, so
 * the refusal is the field check's and not the signature's. Every rewrite
 * keeps the manifest's length, so resign()'s offsets hold. A refused read
 * leaves *out as it found it. */
static void assert_field_refused(uint16_t off, uint8_t was, uint8_t now, tfw_status_t want)
{
    tfw_suit_manifest_t m;
    uint8_t             untouched[sizeof m];

    memcpy(env, SUIT_ENVELOPE, sizeof SUIT_ENVELOPE);
    TEST_ASSERT_EQUAL_HEX8(was, env[off]);
    env[off] = now;
    resign();
    memset(&m, 0xA5, sizeof m);
    memset(untouched, 0xA5, sizeof untouched);
    TEST_ASSERT_EQUAL(want, tfw_suit_read(env, sizeof SUIT_ENVELOPE, &m));
    TEST_ASSERT_EQUAL_MEMORY(untouched, &m, sizeof m);
}

/* suit.c manifest: {1: 1, 2: seq, 3: bstr(common)} at env[124]. */
static void test_a_manifest_that_is_not_a_three_member_map_is_refused(void)
{
    assert_field_refused(124u, 0xA3u, 0xA2u, TFW_ERR_UNSUPPORTED);
}

static void test_a_manifest_version_that_is_not_one_is_refused(void)
{
    assert_field_refused(125u, 0x01u, 0x04u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(126u, 0x01u, 0x02u, TFW_ERR_UNSUPPORTED);   /* the value */
}

/* suit.c uint32_member, through the sequence number. */
static void test_a_sequence_that_is_not_an_unsigned_integer_is_refused(void)
{
    assert_field_refused(127u, 0x02u, 0x05u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(128u, 0x07u, 0x1Bu, TFW_ERR_RANGE);         /* an 8-byte head */
    assert_field_refused(128u, 0x07u, 0x1Cu, TFW_ERR_UNSUPPORTED);   /* a reserved head */
    assert_field_refused(128u, 0x07u, 0x27u, TFW_ERR_UNSUPPORTED);   /* -8 */
}

static void test_a_common_member_that_is_not_a_bstr_is_refused(void)
{
    assert_field_refused(129u, 0x03u, 0x06u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(130u, 0x58u, 0x18u, TFW_ERR_UNSUPPORTED);   /* a uint */
}

/* suit.c common: {2: [[h'00']], 4: bstr(shared sequence)} at env[132]. */
static void test_a_common_map_of_the_wrong_shape_is_refused(void)
{
    assert_field_refused(132u, 0xA2u, 0xA3u, TFW_ERR_UNSUPPORTED);
    assert_field_refused(133u, 0x02u, 0x03u, TFW_ERR_UNSUPPORTED);   /* the components key */
    assert_field_refused(134u, 0x81u, 0x82u, TFW_ERR_UNSUPPORTED);   /* two components */
}

static void test_a_component_id_of_no_parts_or_too_many_is_refused(void)
{
    assert_field_refused(135u, 0x81u, 0x9Cu, TFW_ERR_UNSUPPORTED);   /* a reserved head */
    assert_field_refused(135u, 0x81u, 0x80u, TFW_ERR_UNSUPPORTED);   /* no parts */
    assert_field_refused(135u, 0x81u, 0x85u, TFW_ERR_UNSUPPORTED);   /* five parts */
}

static void test_a_component_part_that_is_not_a_short_bstr_is_refused(void)
{
    assert_field_refused(136u, 0x41u, 0x51u, TFW_ERR_RANGE);         /* 17 bytes */
    assert_field_refused(136u, 0x41u, 0x01u, TFW_ERR_UNSUPPORTED);   /* a uint */
}

static void test_a_shared_sequence_member_that_is_not_a_bstr_is_refused(void)
{
    assert_field_refused(138u, 0x04u, 0x05u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(139u, 0x58u, 0x18u, TFW_ERR_UNSUPPORTED);   /* a uint */
}

/* suit.c shared_seq: [20, {1: vendor, 2: class, 3: digest, 14: size}] at
 * env[141]. */
static void test_a_shared_sequence_of_the_wrong_shape_is_refused(void)
{
    assert_field_refused(141u, 0x82u, 0x83u, TFW_ERR_UNSUPPORTED);
    assert_field_refused(142u, 0x14u, 0x15u, TFW_ERR_UNSUPPORTED);   /* not override-parameters */
    assert_field_refused(143u, 0xA4u, 0xA5u, TFW_ERR_UNSUPPORTED);
}

/* suit.c bstr_fixed, through the vendor and class ids. */
static void test_a_vendor_id_that_is_not_sixteen_bytes_is_refused(void)
{
    assert_field_refused(144u, 0x01u, 0x04u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(145u, 0x50u, 0x4Fu, TFW_ERR_UNSUPPORTED);   /* 15 bytes */
    assert_field_refused(145u, 0x50u, 0x10u, TFW_ERR_UNSUPPORTED);   /* a uint */
}

static void test_a_class_id_that_is_not_sixteen_bytes_is_refused(void)
{
    assert_field_refused(162u, 0x02u, 0x03u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(163u, 0x50u, 0x4Fu, TFW_ERR_UNSUPPORTED);   /* 15 bytes */
    assert_field_refused(163u, 0x50u, 0x10u, TFW_ERR_UNSUPPORTED);   /* a uint */
}

/* suit.c suit_digest, through the image digest: [-16, bstr(32)]. */
static void test_an_image_digest_that_is_not_sha256_is_refused(void)
{
    assert_field_refused(180u, 0x03u, 0x04u, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(181u, 0x58u, 0x18u, TFW_ERR_UNSUPPORTED);   /* a uint */
    assert_field_refused(183u, 0x82u, 0x83u, TFW_ERR_UNSUPPORTED);   /* three members */
    assert_field_refused(184u, 0x2Fu, 0x2Eu, TFW_ERR_UNSUPPORTED);   /* -15 */
    assert_field_refused(185u, 0x58u, 0x18u, TFW_ERR_UNSUPPORTED);   /* a uint */
    assert_field_refused(186u, 0x20u, 0x1Fu, TFW_ERR_UNSUPPORTED);   /* 31 bytes */
}

static void test_an_image_size_that_is_not_an_unsigned_integer_is_refused(void)
{
    assert_field_refused(219u, 0x0Eu, 0x0Fu, TFW_ERR_UNSUPPORTED);   /* the key */
    assert_field_refused(220u, 0x18u, 0x1Bu, TFW_ERR_RANGE);         /* an 8-byte head */
    assert_field_refused(220u, 0x18u, 0x38u, TFW_ERR_UNSUPPORTED);   /* a negative int */
}

/* suit.c hold, the vendor binding: a validly signed manifest naming another
 * vendor reads, and the gate refuses it. The link uses the ATmega2560's
 * profile (mk/host.mk SUIT_TEST_CFLAGS), so the class id and board id held
 * against are the 2560's. */
static void test_a_signed_foreign_vendor_id_is_refused_by_the_gate(void)
{
    tfw_suit_manifest_t m;

    TEST_ASSERT_EQUAL_HEX8(0x50u, env[145]);
    env[146] ^= 0x01u;
    resign();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read(env, sizeof SUIT_ENVELOPE, &m));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                                sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

/* suit.c hold, the size binding's second half: the staged length agrees with
 * the manifest and the trailer's does not. */
static void test_a_size_the_trailer_disagrees_with_is_refused(void)
{
    tr.image_len = sizeof SUIT_IMAGE + 1u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, gate(env, sizeof SUIT_ENVELOPE, SUIT_IMAGE_DIGEST,
                                          sizeof SUIT_IMAGE));
    assert_refused_before_activation();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_vectors_are_what_the_signature_offset_says);
    RUN_TEST(test_a_valid_envelope_reads);
    RUN_TEST(test_a_valid_envelope_passes_the_gate_and_activates);
    RUN_TEST(test_a_flipped_signature_byte_is_refused);
    RUN_TEST(test_a_tampered_manifest_byte_is_refused);
    RUN_TEST(test_a_wrong_image_digest_is_refused);
    RUN_TEST(test_a_trailer_measurement_that_disagrees_is_refused);
    RUN_TEST(test_a_wrong_size_is_refused);
    RUN_TEST(test_a_wrong_class_id_is_refused);
    RUN_TEST(test_a_trailer_for_another_board_is_refused);
    RUN_TEST(test_a_sequence_that_is_not_the_trailers_version_is_refused);
    RUN_TEST(test_a_sequence_below_the_floor_is_refused);
    RUN_TEST(test_a_sequence_at_the_floor_passes);
    RUN_TEST(test_an_unknown_member_is_refused);
    RUN_TEST(test_a_trailing_byte_is_refused);
    RUN_TEST(test_a_truncated_envelope_is_refused);
    RUN_TEST(test_every_prefix_is_refused);
    RUN_TEST(test_an_oversized_length_is_refused);
    RUN_TEST(test_an_eight_byte_length_is_refused);
    RUN_TEST(test_an_envelope_past_the_bound_is_refused);
    RUN_TEST(test_without_the_key_nothing_verifies);
    RUN_TEST(test_a_kid_in_the_unprotected_header_reads);
    RUN_TEST(test_a_resigned_envelope_reads);
    RUN_TEST(test_a_component_id_that_is_not_an_array_is_refused);
    RUN_TEST(test_a_manifest_that_is_not_a_three_member_map_is_refused);
    RUN_TEST(test_a_manifest_version_that_is_not_one_is_refused);
    RUN_TEST(test_a_sequence_that_is_not_an_unsigned_integer_is_refused);
    RUN_TEST(test_a_common_member_that_is_not_a_bstr_is_refused);
    RUN_TEST(test_a_common_map_of_the_wrong_shape_is_refused);
    RUN_TEST(test_a_component_id_of_no_parts_or_too_many_is_refused);
    RUN_TEST(test_a_component_part_that_is_not_a_short_bstr_is_refused);
    RUN_TEST(test_a_shared_sequence_member_that_is_not_a_bstr_is_refused);
    RUN_TEST(test_a_shared_sequence_of_the_wrong_shape_is_refused);
    RUN_TEST(test_a_vendor_id_that_is_not_sixteen_bytes_is_refused);
    RUN_TEST(test_a_class_id_that_is_not_sixteen_bytes_is_refused);
    RUN_TEST(test_an_image_digest_that_is_not_sha256_is_refused);
    RUN_TEST(test_an_image_size_that_is_not_an_unsigned_integer_is_refused);
    RUN_TEST(test_a_signed_foreign_vendor_id_is_refused_by_the_gate);
    RUN_TEST(test_a_size_the_trailer_disagrees_with_is_refused);
    return UNITY_END();
}
