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

void setUp(void) {}
void tearDown(void) {}

/* A well-formed trailer to mutate. Built by the serialiser rather than written
 * out by hand, so the parser is tested against the layout the rest of the tree
 * produces and not against one test's idea of it. */
static void good(uint8_t raw[AG_BOOT_TRAILER_LEN], uint32_t ver, uint32_t len)
{
    ag_boot_trailer_t tr;
    memset(&tr, 0, sizeof tr);
    tr.image_version = ver;
    tr.image_len     = len;
    memset(tr.measurement, 0xA5, sizeof tr.measurement);
    memset(tr.signature,   0x5A, sizeof tr.signature);
    ag_boot_trailer_serialise(&tr, raw);
}

static void test_parses_a_well_formed_trailer(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 7u, 1024u);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_trailer_parse(raw, &tr));
    TEST_ASSERT_EQUAL_UINT32(7u, tr.image_version);
    TEST_ASSERT_EQUAL_UINT32(1024u, tr.image_len);
    TEST_ASSERT_EQUAL_UINT8(0xA5u, tr.measurement[0]);
    TEST_ASSERT_EQUAL_UINT8(0x5Au, tr.signature[63]);
}

static void test_serialise_parse_round_trips(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t in, out;

    memset(&in, 0, sizeof in);
    in.image_version = 0x01020304uL;
    in.image_len     = 0x1234u;
    for (uint8_t i = 0u; i < AG_BOOT_MEASURE_LEN; i++) { in.measurement[i] = i; }
    for (uint8_t i = 0u; i < AG_BOOT_SIG_LEN; i++)     { in.signature[i]   = (uint8_t)(255u - i); }

    ag_boot_trailer_serialise(&in, raw);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_trailer_parse(raw, &out));
    TEST_ASSERT_EQUAL_UINT32(in.image_version, out.image_version);
    TEST_ASSERT_EQUAL_UINT32(in.image_len, out.image_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.measurement, out.measurement, AG_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.signature, out.signature, AG_BOOT_SIG_LEN);
}

/* A blank part reads 0xFF everywhere, which is the commonest way a trailer is
 * absent and the one a bootloader meets first. */
static void test_an_erased_region_is_not_a_trailer(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT_EQUAL_INT(AG_ERR_UNSUPPORTED, ag_boot_trailer_parse(raw, &tr));
}

static void test_a_wrong_magic_is_refused(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 1u, 512u);
    raw[AG_BOOT_TR_MAGIC] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_UNSUPPORTED, ag_boot_trailer_parse(raw, &tr));
}

/* An exact match, not "at least". A future layout might move a field the
 * signature covers, and guessing is how a bootloader verifies one span and
 * measures another. */
static void test_an_unknown_trailer_version_is_refused(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 1u, 512u);
    raw[AG_BOOT_TR_VERSION] = AG_BOOT_TRAILER_VERSION + 1u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_UNSUPPORTED, ag_boot_trailer_parse(raw, &tr));
}

/* An unversioned image opts out of anti-rollback: it can never be superseded
 * and never refused. Refusing it is what stops a tool that forgot to stamp a
 * version from shipping one that quietly cannot be revoked. */
static void test_version_zero_is_refused(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 0u, 512u);
    TEST_ASSERT_EQUAL_INT(AG_ERR_CRYPTO, ag_boot_trailer_parse(raw, &tr));
}

static void test_a_zero_length_image_is_refused(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 1u, 0u);
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE, ag_boot_trailer_parse(raw, &tr));
}

/*
 * THE ONE THAT MATTERS. An image_len reaching the trailer would make the
 * measurement cover the signature field, and the signing tool measuring the
 * same span would produce a trailer that verifies perfectly -- an image that
 * passes every check while the bytes executed are not the bytes signed.
 */
static void test_an_image_may_not_reach_its_own_trailer(void)
{
    uint8_t raw[AG_BOOT_TRAILER_LEN];
    ag_boot_trailer_t tr;

    good(raw, 1u, AG_BOOT_MAX_IMAGE_LEN);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_trailer_parse(raw, &tr));

    good(raw, 1u, AG_BOOT_MAX_IMAGE_LEN + 1u);
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE, ag_boot_trailer_parse(raw, &tr));
}

/* ------------------------------------------------------------- rollback */

static void test_a_version_above_the_floor_is_permitted(void)
{
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_rollback_permit(5u, 3u));
}

/* Reflashing the version already running is a repair, not a downgrade. A rule
 * that refused it would turn a corrupted-but-current device into a brick. */
static void test_a_version_equal_to_the_floor_is_permitted(void)
{
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_rollback_permit(3u, 3u));
}

static void test_a_version_below_the_floor_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(AG_ERR_PROTECTED_REGION, ag_boot_rollback_permit(2u, 3u));
    TEST_ASSERT_EQUAL_INT(AG_ERR_PROTECTED_REGION, ag_boot_rollback_permit(0u, 1u));
}

/*
 * The classic way this check is written wrong: `version - floor` on unsigned
 * arithmetic wraps for every downgrade and compares as an enormous positive
 * number, so the happy path passes and every rollback is accepted.
 */
static void test_a_downgrade_does_not_wrap_into_an_acceptance(void)
{
    TEST_ASSERT_EQUAL_INT(AG_ERR_PROTECTED_REGION,
                          ag_boot_rollback_permit(1u, 0xFFFFFFFFuL));
}

/* ---------------------------------------------------------- floor advance */

static void test_the_floor_rises_to_a_higher_version(void)
{
    uint32_t out = 0u;
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_floor_advance(5u, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(5u, out);
}

static void test_the_floor_never_descends(void)
{
    uint32_t out = 0u;
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_floor_advance(2u, 9u, &out));
    TEST_ASSERT_EQUAL_UINT32(9u, out);

    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_floor_advance(9u, 9u, &out));
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

    TEST_ASSERT_EQUAL_INT(AG_OK,
        ag_boot_floor_advance(3u + AG_BOOT_FLOOR_MAX_STEP, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(3u + AG_BOOT_FLOOR_MAX_STEP, out);

    out = 42u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE,
        ag_boot_floor_advance(4u + AG_BOOT_FLOOR_MAX_STEP, 3u, &out));
    TEST_ASSERT_EQUAL_UINT32(42u, out);     /* untouched */

    out = 42u;
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE, ag_boot_floor_advance(0xFFFFFFFFuL, 0u, &out));
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
    RUN_TEST(test_a_version_above_the_floor_is_permitted);
    RUN_TEST(test_a_version_equal_to_the_floor_is_permitted);
    RUN_TEST(test_a_version_below_the_floor_is_refused);
    RUN_TEST(test_a_downgrade_does_not_wrap_into_an_acceptance);
    RUN_TEST(test_the_floor_rises_to_a_higher_version);
    RUN_TEST(test_the_floor_never_descends);
    RUN_TEST(test_an_oversized_jump_is_refused_and_leaves_the_floor_alone);
    return UNITY_END();
}
