/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The image trailer and the rollback rule.
 *
 * Pure functions of bytes and integers, so every refusal is reachable from a
 * test -- including the ones a bench cannot produce, such as a trailer claiming
 * an image long enough to cover its own signature.
 */

#include <string.h>
#include "unity.h"
#include "boot_image.h"
#include "vectors.h"

void setUp(void) {}
void tearDown(void) {}

/* A well-formed trailer to mutate. Built by the serialiser rather than written
 * out by hand, so the parser is tested against the layout the rest of the tree
 * produces and not against one test's idea of it. */
static void good(uint8_t raw[TFW_BOOT_TRAILER_LEN], uint32_t ver, uint32_t len)
{
    tfw_boot_trailer_t tr;
    memset(&tr, 0, sizeof tr);
    tr.image_version = ver;
    tr.image_len     = len;
    tr.board_id      = (uint8_t)TFW_BOARD_ID;
    memset(tr.measurement, 0xA5, sizeof tr.measurement);
    memset(tr.signature,   0x5A, sizeof tr.signature);
    tfw_boot_trailer_serialise(&tr, raw);
}

static void test_parses_a_well_formed_trailer(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    good(raw, 7u, 1024u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_UINT32(7u, tr.image_version);
    TEST_ASSERT_EQUAL_UINT32(1024u, tr.image_len);
    TEST_ASSERT_EQUAL_UINT8(0xA5u, tr.measurement[0]);
    TEST_ASSERT_EQUAL_UINT8(0x5Au, tr.signature[63]);
}

static void test_serialise_parse_round_trips(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t in, out;

    memset(&in, 0, sizeof in);
    in.image_version = 0x01020304uL;
    in.image_len     = 0x1234u;
    in.board_id      = 0x81u;
    in.image_flags   = TFW_BOOT_IMAGE_FLAG_DEV;
    for (uint8_t i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) { in.measurement[i] = i; }
    for (uint8_t i = 0u; i < TFW_BOOT_SIG_LEN; i++)     { in.signature[i]   = (uint8_t)(255u - i); }

    tfw_boot_trailer_serialise(&in, raw);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &out));
    TEST_ASSERT_EQUAL_UINT32(in.image_version, out.image_version);
    TEST_ASSERT_EQUAL_UINT32(in.image_len, out.image_len);
    TEST_ASSERT_EQUAL_HEX8(in.board_id, out.board_id);
    TEST_ASSERT_EQUAL_HEX8(in.image_flags, out.image_flags);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.measurement, out.measurement, TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.signature, out.signature, TFW_BOOT_SIG_LEN);
}

/* A blank part reads 0xFF everywhere, which is the commonest way a trailer is
 * absent and the one a bootloader meets first. */
static void test_an_erased_region_is_not_a_trailer(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_trailer_parse(raw, &tr));
}

static void test_a_wrong_magic_is_refused(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    good(raw, 1u, 512u);
    raw[TFW_BOOT_TR_MAGIC] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_trailer_parse(raw, &tr));
}

/* An exact match, not "at least". A future layout might move a field the
 * signature covers, and guessing is how a bootloader verifies one span and
 * measures another. */
static void test_an_unknown_trailer_version_is_refused(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    good(raw, 1u, 512u);
    raw[TFW_BOOT_TR_VERSION] = TFW_BOOT_TRAILER_VERSION + 1u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_trailer_parse(raw, &tr));
}

/* A trailer that passes magic and version and is then refused writes nothing:
 * the image_version and image_len checks run before any field reaches *out. */
static void refused_leaves_trailer(const uint8_t head[16], tfw_status_t want)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr, sentinel;

    memset(raw, 0, sizeof raw);
    memcpy(raw, head, 16);
    memset(&tr, 0xA5, sizeof tr);
    memcpy(&sentinel, &tr, sizeof sentinel);
    TEST_ASSERT_EQUAL(want, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &tr, sizeof tr);
}

/* An unversioned image opts out of anti-rollback: it can never be superseded
 * and never refused. Refusing it is what stops a tool that forgot to stamp a
 * version from shipping one that quietly cannot be revoked. */
static void test_version_zero_is_refused(void)
{
    static const uint8_t head[16] = {
        0x54, 0x46, 0x49, 0x4D, 0x01, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    };
    refused_leaves_trailer(head, TFW_ERR_CRYPTO);
}

static void test_a_zero_length_image_is_refused(void)
{
    static const uint8_t head[16] = {
        0x54, 0x46, 0x49, 0x4D, 0x01, 0x00, 0x02, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    refused_leaves_trailer(head, TFW_ERR_RANGE);
}

/*
 * THE ONE THAT MATTERS. An image_len reaching the trailer would make the
 * measurement cover the signature field, and the signing tool measuring the
 * same span would produce a trailer that verifies perfectly -- an image that
 * passes every check while the bytes executed are not the bytes signed.
 */
static void test_an_image_may_not_reach_its_own_trailer(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    good(raw, 1u, TFW_BOOT_MAX_IMAGE_LEN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));

    good(raw, 1u, TFW_BOOT_MAX_IMAGE_LEN + 1u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_trailer_parse(raw, &tr));
}

/* The reserved bytes are outside the signed span; a set one is a trailer this
 * build does not know, refused at the parse and leaving *out as it was. Each
 * byte, first to last, one at a time and each bit of it. */
static void test_a_set_reserved_byte_is_refused(void)
{
    uint8_t raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr, sentinel;

    memset(&sentinel, 0xA5, sizeof sentinel);
    for (uint8_t i = TFW_BOOT_TR_RESERVED; i < TFW_BOOT_TRAILER_LEN; i++) {
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            good(raw, 1u, 512u);
            raw[i] = (uint8_t)(1u << bit);
            memcpy(&tr, &sentinel, sizeof tr);
            TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_trailer_parse(raw, &tr));
            TEST_ASSERT_EQUAL_MEMORY(&sentinel, &tr, sizeof tr);
        }
    }

    good(raw, 1u, 512u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
}

/* ------------------------------------------------------------- binding */

/* A vector's signed span in a whole trailer, signature and tail zeroed: the
 * parse and the binding never read past byte 48. */
static void from_vector(uint8_t raw[TFW_BOOT_TRAILER_LEN], const char *span)
{
    memset(raw, 0, TFW_BOOT_TRAILER_LEN);
    memcpy(raw, span, TFWV_TRAILER_SPAN_LEN);
}

/* The serialiser's bytes for a vector's fields, against the frozen span. The
 * vectors came from Python's struct, so this is the C layout held to an
 * independent one, not to itself. */
static void assert_serialises_to(const char *span, uint8_t board_id, uint8_t flags)
{
    static const uint8_t abc[TFW_BOOT_MEASURE_LEN] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40,
        0xDE, 0x5D, 0xAE, 0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17,
        0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD };
    tfw_boot_trailer_t tr;
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];

    memset(&tr, 0, sizeof tr);
    tr.image_version = 3u;
    tr.image_len     = 0x1000u;
    tr.board_id      = board_id;
    tr.image_flags   = flags;
    memcpy(tr.measurement, abc, sizeof abc);
    tfw_boot_trailer_serialise(&tr, raw);
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)span, raw, TFWV_TRAILER_SPAN_LEN);
}

/* Every board with a profile: its span is the layout, parses, carries its id,
 * and is permitted on that board and on no other. */
static void test_each_boards_trailer_binds_to_that_board_alone(void)
{
    static const struct { const char *span; uint8_t id; } rows[] = {
        { TFWV_TRAILER_ATMEGA328P_SPAN, 0x01u },
        { TFWV_TRAILER_ATMEGA2560_SPAN, 0x02u },
        { TFWV_TRAILER_ATMEGA32U4_SPAN, 0x03u },
        { TFWV_TRAILER_ATTINY85_SPAN,   0x04u },
        { TFWV_TRAILER_ESP32P4POE_SPAN, 0x80u },
        { TFWV_TRAILER_ESP32QEMU_SPAN,  0x81u },
    };
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    for (uint8_t i = 0u; i < sizeof rows / sizeof rows[0]; i++) {
        assert_serialises_to(rows[i].span, rows[i].id, 0u);
        from_vector(raw, rows[i].span);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
        TEST_ASSERT_EQUAL_HEX8(rows[i].id, tr.board_id);
        TEST_ASSERT_EQUAL_HEX8(0u, tr.image_flags);
        TEST_ASSERT_EQUAL_UINT32(3u, tr.image_version);
        TEST_ASSERT_EQUAL_UINT32(0x1000u, tr.image_len);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_binding_permit(&tr, rows[i].id));
        for (uint8_t j = 0u; j < sizeof rows / sizeof rows[0]; j++) {
            if (j != i) {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                                      tfw_boot_binding_permit(&tr, rows[j].id));
            }
        }
    }
}

/* A genuine 2560 image offered to a 328P. Well-formed, so the parse takes it;
 * not this board's, so the binding refuses it. */
static void test_an_image_for_another_board_is_refused(void)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    assert_serialises_to(TFWV_TRAILER_MISMATCH_SPAN, 0x02u, 0u);
    from_vector(raw, TFWV_TRAILER_MISMATCH_SPAN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x01u));
}

/* board_id 0 is every trailer from before the field: refused on every board,
 * and on a part built without a profile, whose own id is also 0. */
static void test_an_unbound_image_is_refused_everywhere(void)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    assert_serialises_to(TFWV_TRAILER_BOARDZERO_SPAN, 0x00u, 0u);
    from_vector(raw, TFWV_TRAILER_BOARDZERO_SPAN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_HEX8(0u, tr.board_id);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x00u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x01u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
                          tfw_boot_binding_permit(&tr, (uint8_t)TFW_BOARD_ID));
}

/* A reserved flag set is refused, the frozen vector's bit 1 and every other
 * reserved bit alike. */
static void test_a_reserved_image_flag_is_refused(void)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    assert_serialises_to(TFWV_TRAILER_RESERVEDFLAG_SPAN, 0x01u, 0x02u);
    from_vector(raw, TFWV_TRAILER_RESERVEDFLAG_SPAN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x01u));

    for (uint8_t bit = 1u; bit < 8u; bit++) {
        tr.image_flags = (uint8_t)(1u << bit);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x01u));
        tr.image_flags |= TFW_BOOT_IMAGE_FLAG_DEV;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_boot_binding_permit(&tr, 0x01u));
    }
}

/* Bit 0 is defined: parsed, carried, and not judged by the binding. */
static void test_a_development_image_is_parsed_and_carried(void)
{
    uint8_t            raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    assert_serialises_to(TFWV_TRAILER_DEVIMAGE_SPAN, 0x01u, TFW_BOOT_IMAGE_FLAG_DEV);
    from_vector(raw, TFWV_TRAILER_DEVIMAGE_SPAN);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_IMAGE_FLAG_DEV, tr.image_flags);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_binding_permit(&tr, 0x01u));
}

/* ------------------------------------------------------------- rollback */

static void test_a_version_above_the_floor_is_permitted(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_rollback_permit(5u, 3u));
}

/* Reflashing the version already running is a repair, not a downgrade. A rule
 * that refused it would turn a corrupted-but-current device into a brick. */
static void test_a_version_equal_to_the_floor_is_permitted(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_rollback_permit(3u, 3u));
}

static void test_a_version_below_the_floor_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_rollback_permit(2u, 3u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_rollback_permit(0u, 1u));
}

/*
 * The classic way this check is written wrong: `version - floor` on unsigned
 * arithmetic wraps for every downgrade and compares as an enormous positive
 * number, so the happy path passes and every rollback is accepted.
 */
static void test_a_downgrade_does_not_wrap_into_an_acceptance(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_boot_rollback_permit(1u, 0xFFFFFFFFuL));
}

/* ---------------------------------------------------------- floor advance */

static void test_the_floor_rises_to_a_higher_version(void)
{
    uint32_t out = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_floor_advance(5u, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(5u, out);
}

static void test_the_floor_never_descends(void)
{
    uint32_t out = 0u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_floor_advance(2u, 9u, &out));
    TEST_ASSERT_EQUAL_UINT32(9u, out);

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_floor_advance(9u, 9u, &out));
    TEST_ASSERT_EQUAL_UINT32(9u, out);
}

/*
 * An absurd version would spend the element's whole two-million-increment
 * counter budget in one update and leave anti-rollback permanently stuck --
 * from a SIGNED image, with no way back, because the counter does not descend.
 */
static void test_an_oversized_jump_is_refused_and_leaves_the_floor_alone(void)
{
    uint32_t out = 42u;

    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_floor_advance(3u + TFW_BOOT_FLOOR_MAX_STEP, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(3u + TFW_BOOT_FLOOR_MAX_STEP, out);

    out = 42u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
        tfw_boot_floor_advance(4u + TFW_BOOT_FLOOR_MAX_STEP, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(42u, out);     /* untouched */

    out = 42u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_floor_advance(0xFFFFFFFFuL, 0u, &out));
    TEST_ASSERT_EQUAL_UINT32(42u, out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_a_well_formed_trailer);
    RUN_TEST(test_serialise_parse_round_trips);
    RUN_TEST(test_an_erased_region_is_not_a_trailer);
    RUN_TEST(test_a_wrong_magic_is_refused);
    RUN_TEST(test_an_unknown_trailer_version_is_refused);
    RUN_TEST(test_version_zero_is_refused);
    RUN_TEST(test_a_zero_length_image_is_refused);
    RUN_TEST(test_an_image_may_not_reach_its_own_trailer);
    RUN_TEST(test_a_set_reserved_byte_is_refused);
    RUN_TEST(test_each_boards_trailer_binds_to_that_board_alone);
    RUN_TEST(test_an_image_for_another_board_is_refused);
    RUN_TEST(test_an_unbound_image_is_refused_everywhere);
    RUN_TEST(test_a_reserved_image_flag_is_refused);
    RUN_TEST(test_a_development_image_is_parsed_and_carried);
    RUN_TEST(test_a_version_above_the_floor_is_permitted);
    RUN_TEST(test_a_version_equal_to_the_floor_is_permitted);
    RUN_TEST(test_a_version_below_the_floor_is_refused);
    RUN_TEST(test_a_downgrade_does_not_wrap_into_an_acceptance);
    RUN_TEST(test_the_floor_rises_to_a_higher_version);
    RUN_TEST(test_the_floor_never_descends);
    RUN_TEST(test_an_oversized_jump_is_refused_and_leaves_the_floor_alone);
    return UNITY_END();
}
