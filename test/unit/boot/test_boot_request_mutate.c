/*
 * boot/boot_request.c under mutation (#163): the update request the
 * application leaves in the handoff region before it resets into the boot.
 *
 * test_boot_request checks the request a well-behaved application writes.
 * This suite pins the encoding byte for byte and holds the check against
 * every reset cause and every single-bit damage to the eight bytes.
 *
 * Mutants this suite kills (each tried against the unit and seen to fail it):
 *   R1  valid: the reset-cause check dropped
 *   R2  valid: `!= WATCHDOG` -> `< WATCHDOG` (a later cause honoured)
 *   R3  valid: the magic compare inverted
 *   R4  valid: the magic check dropped
 *   R5  valid: the inverse check dropped (any inverse honoured)
 *   R6  valid: the inverse read at the magic's offset
 *   R7  encode: the magic and the inverse written at each other's offset
 *   R8  encode: the inverse not written
 *   R9  encode: the magic written in both places
 */
#include <string.h>
#include "unity.h"
#include "boot_request.h"

/* The request as it is on the wire, little-endian: 'U','P','D','!' and its
 * complement. A literal, so a moved encoding fails here. */
static const uint8_t WIRE[TFW_BOOT_REQUEST_LEN] = {
    0x55u, 0x50u, 0x44u, 0x21u, 0xAAu, 0xAFu, 0xBBu, 0xDEu
};

void setUp(void) {}
void tearDown(void) {}

/* R7-R9 */
static void test_encode_writes_the_literal_and_nothing_else(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN + 8u];
    uint8_t want[sizeof region];

    memset(region, 0x5Au, sizeof region);
    memset(want, 0x5Au, sizeof want);
    memcpy(&want[4], WIRE, sizeof WIRE);
    tfw_boot_request_encode(&region[4]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, region, sizeof region);
}

/* R1, R2: the literal is honoured on a watchdog reset and on no other cause,
 * including the values past the named ones. */
static void test_valid_only_on_a_watchdog_reset(void)
{
    for (unsigned c = 0u; c < 16u; c++) {
        TEST_ASSERT_EQUAL_UINT8(c == (unsigned)TFW_RESET_WATCHDOG ? 1u : 0u,
                                tfw_boot_request_valid(WIRE, (tfw_reset_cause_t)c));
    }
}

/* R3-R6: every single-bit damage to either word is refused. */
static void test_every_single_bit_damage_is_refused(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    for (unsigned bit = 0u; bit < 8u * TFW_BOOT_REQUEST_LEN; bit++) {
        memcpy(region, WIRE, sizeof region);
        region[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    }
}

/* R4-R6: the magic alone, the inverse alone, the two swapped, and the magic
 * in both places are each refused. */
static void test_half_a_request_is_refused(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    memset(region, 0, sizeof region);
    memcpy(region, WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memset(region, 0, sizeof region);
    memcpy(&region[4], &WIRE[4], 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memcpy(region, &WIRE[4], 4u);
    memcpy(&region[4], WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memcpy(region, WIRE, 4u);
    memcpy(&region[4], WIRE, 4u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));

    memset(region, 0xFFu, sizeof region);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    memset(region, 0x00u, sizeof region);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
}

/* What encode writes, valid honours: the two agree. */
static void test_encode_round_trips_through_valid(void)
{
    uint8_t region[TFW_BOOT_REQUEST_LEN];

    memset(region, 0, sizeof region);
    tfw_boot_request_encode(region);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_request_valid(region, TFW_RESET_WATCHDOG));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_request_valid(region, TFW_RESET_SOFTWARE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_writes_the_literal_and_nothing_else);
    RUN_TEST(test_valid_only_on_a_watchdog_reset);
    RUN_TEST(test_every_single_bit_damage_is_refused);
    RUN_TEST(test_half_a_request_is_refused);
    RUN_TEST(test_encode_round_trips_through_valid);
    return UNITY_END();
}
