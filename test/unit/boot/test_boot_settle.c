/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The settle of a confirmed trial against the floor's authority: the real
 * boot_store.c over the mock EEPROM, with the element's counter played by
 * this file. GOOD reaches EEPROM only once the authority holds the new floor
 * (decision 5); an element that refuses the write leaves the block CONFIRMED,
 * and the next reset retries the advance.
 */
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_cdi.h"
#include "boot_platform.h"
#include "boot_store.h"
#include "boot_trial.h"

/* The element's monotonic counter. It advances one increment at a time, as
 * tfw_boot_floor_write drives the part, and fails once `incr_left` increments
 * have been spent -- part way through or before the first. */
static int      el_present;
static uint32_t el_counter;
static int      el_fail;
static unsigned el_incr_left;
static unsigned el_writes;
static int      el_read_fail;

tfw_status_t tfw_boot_floor_read(uint32_t *out)
{
    if (!el_present) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (el_read_fail) {
        return TFW_ERR_CRYPTO;         /* as cdi_atecc.c reports any read error */
    }
    *out = el_counter;
    return TFW_OK;
}

tfw_status_t tfw_boot_floor_write(uint32_t value)
{
    if (!el_present) {
        return TFW_ERR_UNSUPPORTED;
    }
    el_writes++;
    while (el_counter < value) {
        if (el_fail && el_incr_left == 0u) {
            el_fail = 0;                        /* once: the bus recovers */
            return TFW_ERR_CRYPTO;
        }
        if (el_incr_left > 0u) {
            el_incr_left--;
        }
        el_counter++;
    }
    return TFW_OK;
}

static tfw_boot_platform_t plat;

void setUp(void)
{
    tfw_boot_mock_reset();
    memset(&plat, 0, sizeof plat);
    el_present   = 0;
    el_counter   = 0u;
    el_fail      = 0;
    el_incr_left = 0u;
    el_writes    = 0u;
    el_read_fail = 0;
}
void tearDown(void) {}

/* The block as the next reset finds it. */
static void reload(void)
{
    tfw_status_t st;

    memset(&plat, 0, sizeof plat);
    tfw_boot_platform_load(&plat, &st);
    TEST_ASSERT_EQUAL(TFW_OK, st);
}

/* Writes a block holding `boot_state` and `ee_floor` to the mock EEPROM and
 * loads it, as a reset does. */
static void reset_with(uint8_t boot_state, uint32_t ee_floor)
{
    uint8_t ee[TFW_BOOT_EE_LEN];

    memset(ee, 0xFF, sizeof ee);
    memset(&plat, 0, sizeof plat);
    plat.boot_state     = boot_state;
    plat.ota_state      = TFW_BOOT_OTA_IDLE;
    plat.rollback_floor = ee_floor;
    tfw_boot_platform_serialise(&plat, ee);
    memcpy(tfw_boot_mock_nvm(), ee, TFW_BOOT_EE_LEN);
    reload();
}

/* ------------------------------------------------------- the store contract */

void test_store_reports_an_element_error_and_writes_no_eeprom(void)
{
    uint8_t before[TFW_BOOT_EE_LEN];

    el_present = 1;
    el_counter = 3u;
    el_fail    = 1;
    reset_with(TFW_BOOT_STATE_GOOD, 0u);
    memcpy(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_store(&plat, 7u));
    TEST_ASSERT_EQUAL_UINT32(3u, el_counter);
    TEST_ASSERT_EQUAL_UINT32(0u, plat.rollback_floor);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);
}

void test_store_to_the_element_leaves_eeprom_alone(void)
{
    uint8_t before[TFW_BOOT_EE_LEN];

    el_present = 1;
    el_counter = 3u;
    reset_with(TFW_BOOT_STATE_GOOD, 0u);
    memcpy(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_store(&plat, 7u));
    TEST_ASSERT_EQUAL_UINT32(7u, el_counter);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);
}

/* No element: EEPROM is the authority, and holding the value there is TFW_OK. */
void test_store_without_an_element_is_the_eeprom_copy(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_GOOD, 3u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_store(&plat, 7u));
    reload();
    TEST_ASSERT_EQUAL_UINT32(7u, plat.rollback_floor);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
}

/* ------------------------------------------------------- the load contract */

/* #82, the read side: an element that is there and fails the read is an error,
 * never the EEPROM floor -- here an EEPROM copy that would pass anything. */
void test_load_reports_an_element_read_error_and_not_the_eeprom_floor(void)
{
    uint32_t floor = 0xA5A5A5A5u;

    el_present   = 1;
    el_counter   = 7u;
    el_read_fail = 1;
    reset_with(TFW_BOOT_STATE_GOOD, 0u);

    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(0xA5A5A5A5u, floor);        /* not written */
}

/* The element's counter wins over the EEPROM copy, whichever is larger. */
void test_load_with_an_element_is_the_counter(void)
{
    uint32_t floor;

    el_present = 1;
    el_counter = 3u;
    reset_with(TFW_BOOT_STATE_GOOD, 9u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);
}

/* No element: TFW_ERR_UNSUPPORTED from the tier, and the soft tier reads EEPROM. */
void test_load_without_an_element_is_the_eeprom_copy(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_GOOD, 9u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(9u, floor);
}

/* --------------------------------------------------------- the element tier */

void test_settle_advances_the_counter_then_writes_good(void)
{
    uint32_t floor;

    el_present = 1;
    el_counter = 3u;
    reset_with(TFW_BOOT_STATE_CONFIRMED, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
    TEST_ASSERT_EQUAL_UINT32(7u, el_counter);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(0u, plat.rollback_floor);   /* EEPROM is moot */
}

/* The finding on #82: a transient I2C error on the counter write. The block
 * stays CONFIRMED, so the next reset settles again -- and that one advances
 * the counter and writes GOOD. */
void test_a_refused_counter_write_keeps_confirmed_and_the_next_reset_retries(void)
{
    uint8_t  before[TFW_BOOT_EE_LEN];
    uint32_t floor;

    el_present = 1;
    el_counter = 3u;
    el_fail    = 1;
    reset_with(TFW_BOOT_STATE_CONFIRMED, 0u);
    memcpy(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);                 /* not advanced */
    TEST_ASSERT_EQUAL_UINT32(3u, el_counter);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, plat.boot_state);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, tfw_boot_mock_nvm(), TFW_BOOT_EE_LEN);

    /* the next reset */
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, plat.boot_state);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_trial_at_reset(&plat));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
    TEST_ASSERT_EQUAL_UINT32(7u, el_counter);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&plat));
}

/* The counter stopped part way: the next reset carries it on from there. */
void test_a_counter_stopped_part_way_is_carried_on_at_the_next_reset(void)
{
    uint32_t floor;

    el_present   = 1;
    el_counter   = 3u;
    el_fail      = 1;
    el_incr_left = 2u;
    reset_with(TFW_BOOT_STATE_CONFIRMED, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(5u, el_counter);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_CONFIRMED, plat.boot_state);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, el_counter);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
}

/* The counter was written and the reset came before GOOD: the advance is no
 * step, the element is not asked, and only GOOD is written. */
void test_a_reset_after_the_counter_write_only_writes_good(void)
{
    uint32_t floor;

    el_present = 1;
    el_counter = 7u;
    el_fail    = 1;
    reset_with(TFW_BOOT_STATE_CONFIRMED, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT(0u, el_writes);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
}

/* ------------------------------------------------------------ the soft tier */

/* No element: the floor and GOOD settle in the one boot. */
void test_the_soft_tier_settles_to_good_in_one_boot(void)
{
    uint32_t floor;

    reset_with(TFW_BOOT_STATE_CONFIRMED, 3u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_floor_load(&plat, &floor));
    TEST_ASSERT_EQUAL_UINT32(3u, floor);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_settle(&plat, 7u, &floor));
    TEST_ASSERT_EQUAL_UINT32(7u, floor);
    reload();
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_GOOD, plat.boot_state);
    TEST_ASSERT_EQUAL_UINT32(7u, plat.rollback_floor);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&plat));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_store_reports_an_element_error_and_writes_no_eeprom);
    RUN_TEST(test_store_to_the_element_leaves_eeprom_alone);
    RUN_TEST(test_store_without_an_element_is_the_eeprom_copy);
    RUN_TEST(test_load_reports_an_element_read_error_and_not_the_eeprom_floor);
    RUN_TEST(test_load_with_an_element_is_the_counter);
    RUN_TEST(test_load_without_an_element_is_the_eeprom_copy);
    RUN_TEST(test_settle_advances_the_counter_then_writes_good);
    RUN_TEST(test_a_refused_counter_write_keeps_confirmed_and_the_next_reset_retries);
    RUN_TEST(test_a_counter_stopped_part_way_is_carried_on_at_the_next_reset);
    RUN_TEST(test_a_reset_after_the_counter_write_only_writes_good);
    RUN_TEST(test_the_soft_tier_settles_to_good_in_one_boot);
    return UNITY_END();
}
