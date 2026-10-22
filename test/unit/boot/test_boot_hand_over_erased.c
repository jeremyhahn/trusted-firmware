/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TCG DICE HW Req 6.4 at the hand-over of an unprovisioned device: with the
 * UDS slot erased (all 0xFF), tfw_boot_hand_over (boot/boot_main.c) still
 * boots, but no CDI derived from erased flash reaches the application. The
 * handoff is a valid block whose CDI is the zero sentinel, the tier the
 * bootloader found beside it, and every buffer that held the slot's bytes is
 * cleared before the jump.
 *
 * Linked as test_boot_hand_over_clear is, except that cdi_soft.c is built as
 * it ships, its slot left erased. kErasedCdi is HMAC-SHA256(32 x 0xFF,
 * 0x20..0x3F), the CDI a derive that ignored the erased check would make,
 * computed with Python's hmac and hashlib, not with the code under test.
 */
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "cdi_residue.h"
#include "crc16.h"
#include "tfw/boot.h"

static unsigned derives;
static int      slot_at_return;
static int      cdi_at_return;

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_residue_reset();
    derives        = 0u;
    slot_at_return = 0;
    cdi_at_return  = 0;
}
void tearDown(void) {}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)floor;
    (void)out;
    TEST_FAIL_MESSAGE("tfw_boot_hand_over called tfw_boot_evaluate");
    return TFW_ERR_BAD_STATE;
}

static const uint8_t kErasedCdi[TFW_BOOT_MEASURE_LEN] = {
    0x56, 0x1f, 0x14, 0x78, 0x04, 0xa6, 0x37, 0x05, 0x06, 0xdb, 0x9e, 0xd5, 0x4e, 0xc0, 0x08, 0x35,
    0x40, 0xb5, 0xad, 0x50, 0xf3, 0x7c, 0x06, 0xae, 0xa2, 0xf9, 0x9b, 0x27, 0xd6, 0xa4, 0x32, 0x5f,
};

static const uint8_t kErased[TFW_BOOT_MEASURE_LEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static const uint8_t kZero[TFW_BOOT_MEASURE_LEN];

/* What the stack held the moment the derive returned, read before the
 * hand-over's next call reuses the derive's frame. -Wl,--wrap (mk/boot.mk)
 * sends boot_main.c's call here; the snapshot's frame takes the derive's
 * place, so its window holds what the derive left. Set by setUp. */
tfw_status_t __real_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN]);
tfw_status_t __wrap_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN]);

tfw_status_t __wrap_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN])
{
    tfw_status_t st = __real_tfw_boot_cdi_derive(measurement, cdi);

    tfw_residue_snapshot();
    derives++;
    slot_at_return = tfw_residue_on_stack(kErased, sizeof kErased);
    cdi_at_return  = tfw_residue_on_stack(kErasedCdi, sizeof kErasedCdi);
    return st;
}

static void verdict(tfw_boot_verdict_t *v)
{
    uint8_t i;

    memset(v, 0, sizeof *v);
    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        v->measurement[i] = (uint8_t)(0x20u + i);
    }
    v->trailer.image_version = 0x01020304uL;
}

/* The block the application reads: valid, so it is taken as tfw-boot's, with
 * the zero CDI that src/core/handoff.c reads as "unprovisioned" and the
 * all-zero DeviceID seed. */
static void test_the_handoff_marks_the_identity_absent(void)
{
    tfw_boot_verdict_t v;
    const uint8_t     *h = tfw_boot_mock_handoff();
    uint16_t           crc;

    verdict(&v);
    tfw_boot_hand_over(&v);

    TEST_ASSERT_EQUAL_UINT(1u, tfw_boot_mock_jumped());
    TEST_ASSERT_EQUAL_HEX8(0x41u, h[TFW_BOOT_HO_MAGIC]);
    TEST_ASSERT_EQUAL_HEX8(0x49u, h[TFW_BOOT_HO_MAGIC + 3u]);
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SOFTWARE, h[TFW_BOOT_HO_TIER]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kZero, &h[TFW_BOOT_HO_CDI], sizeof kZero);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v.measurement, &h[TFW_BOOT_HO_MEASUREMENT], TFW_BOOT_MEASURE_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kZero, &h[TFW_BOOT_HO_DEVICE_SEED], TFW_BOOT_SEED_LEN);
    /* Version 4: the CRC covers what is before it, then the seed after it. */
    crc = tfw_crc16_from(tfw_crc16(h, TFW_BOOT_HO_CRC), &h[TFW_BOOT_HO_DEVICE_SEED],
                         (uint16_t)(TFW_BOOT_HANDOFF_LEN - TFW_BOOT_HO_DEVICE_SEED));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFFu), h[TFW_BOOT_HO_CRC]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), h[TFW_BOOT_HO_CRC + 1u]);
}

static void test_no_cdi_from_the_erased_slot_is_in_the_handoff(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);

    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_in(tfw_boot_mock_handoff(), TFW_BOOT_HANDOFF_LEN,
                                             kErasedCdi, sizeof kErasedCdi),
                              "a CDI derived from the erased slot");
}

/* The derive's copy of the slot is looked for as the derive returns, where it
 * lies until the hand-over's next call overwrites it, and again straight after
 * the hand-over, as tfw_residue_snapshot requires. */
static void test_the_hand_over_leaves_no_slot_bytes_on_the_stack(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, derives, "derives");
    TEST_ASSERT_FALSE_MESSAGE(cdi_at_return, "erased CDI as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(slot_at_return, "slot bytes as the derive returns");
    tfw_residue_snapshot();

    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kErasedCdi, sizeof kErasedCdi), "erased CDI");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kErased, sizeof kErased), "slot bytes");
}

/* Every clear on this path, newest first: the block, `cdi` before the jump,
 * the seed derive's copy of the slot (include/tfw/boot.h version 4: it reads
 * the slot again and refuses it too), `cdi` on the refused derive, then the
 * derive's copy of the slot. The first, of `cdi` before the derive, holds
 * whatever the stack did and is not looked for. Six in all: no HMAC ran, so
 * none of its clears is here. */
static void test_the_hand_over_clears_every_buffer_in_order(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(6u, tfw_residue_zero_calls(), "clears");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(0u, TFW_BOOT_HANDOFF_LEN, kZero, sizeof kZero),
                             "block");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(1u, TFW_BOOT_MEASURE_LEN, kZero, sizeof kZero),
                             "cdi before the jump");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(2u, TFW_BOOT_MEASURE_LEN, kErased, sizeof kErased),
                             "the seed derive's slot copy");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(3u, TFW_BOOT_MEASURE_LEN, kZero, sizeof kZero),
                             "cdi on the refused derive");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(4u, TFW_BOOT_MEASURE_LEN, kErased, sizeof kErased),
                             "the slot's copy");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_handoff_marks_the_identity_absent);
    RUN_TEST(test_no_cdi_from_the_erased_slot_is_in_the_handoff);
    RUN_TEST(test_the_hand_over_leaves_no_slot_bytes_on_the_stack);
    RUN_TEST(test_the_hand_over_clears_every_buffer_in_order);
    return UNITY_END();
}
