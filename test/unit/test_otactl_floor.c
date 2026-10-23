/* OTA_INFO's rollback floor, each tier's reader called by its own name: the
 * platform block's floor (src/core/ota_floor_eeprom.c) and the element's boot
 * counter (src/core/ota_floor_atecc.c), both renamed by mk/firmware.mk's
 * FLOOR_RENAME exactly as a CRYPTO=auto image links them. The dispatch between
 * them is test_crypto_auto's.
 *
 * Neither reader compares a version against the floor: each only reports it,
 * so a version under, at or over the floor is the caller's question. What is
 * pinned here is that the floor reported is exactly the one in force, at its
 * edges, and that a failed read reports no floor. */
#include <stdint.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "../../src/core/ota_floor.h"

tfw_status_t tfw_otactl_floor_element(const tfw_boot_platform_t *p, uint32_t *out);
tfw_status_t tfw_otactl_floor_soft(const tfw_boot_platform_t *p, uint32_t *out);

static tfw_boot_platform_t plat;

void setUp(void)
{
    tfw_model_reset();
    plat.rollback_floor = 0u;
}
void tearDown(void) {}

/* ------------------------------------------------------------------ soft */

static void soft_reports(uint32_t floor)
{
    uint32_t out = 0xA5A5A5A5u;

    plat.rollback_floor = floor;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor_soft(&plat, &out));
    TEST_ASSERT_EQUAL_HEX32(floor, out);
}

void test_soft_reports_the_platform_floor(void)
{
    soft_reports(7u);
}

void test_soft_reports_a_zero_floor(void)
{
    soft_reports(0u);
}

void test_soft_reports_the_top_floor(void)
{
    soft_reports(0xFFFFFFFFu);
}

/* The soft tier's floor is a field of the platform block already in RAM: it
 * has no failed read. With the element present it sends the part no command
 * and leaves it asleep; with the element gone it still answers TFW_OK
 * (src/core/ota_floor_eeprom.c:10). */
void test_soft_does_not_touch_the_element(void)
{
    soft_reports(3u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    tfw_model_set_absent(1u);
    soft_reports(3u);
}

/* --------------------------------------------------------------- element */

static void element_reports(uint32_t counter)
{
    uint32_t out = 0xA5A5A5A5u;

    tfw_model_set_counter(counter);
    plat.rollback_floor = 7u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_HEX32(counter, out);
}

void test_element_reports_the_boot_counter(void)
{
    element_reports(42u);
}

void test_element_reports_a_zero_counter(void)
{
    element_reports(0u);
}

/* All four little-endian bytes land in their place. */
void test_element_reports_every_counter_byte(void)
{
    element_reports(0x00123456u);
}

/* The platform block's floor is never the element tier's answer. */
void test_element_ignores_the_platform_floor(void)
{
    uint32_t out = 0u;

    tfw_model_set_counter(2u);
    plat.rollback_floor = 9u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_UINT32(2u, out);
}

/* No element on the bus: the wake fails (src/core/ota_floor_atecc.c:17) and no
 * floor is written. */
void test_element_without_a_part_reports_no_floor(void)
{
    uint32_t out = 0xA5A5A5A5u;

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, out);
}

/* The part wakes but refuses the Counter read (src/core/ota_floor_atecc.c:23). */
void test_element_refused_read_reports_no_floor(void)
{
    uint32_t out = 0xA5A5A5A5u;

    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, out);
}

/* The reader puts the part back to sleep, read or not. */
void test_element_sleeps_after_the_read(void)
{
    uint32_t out = 0u;

    tfw_model_set_counter(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());

    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_otactl_floor_element(&plat, &out));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_soft_reports_the_platform_floor);
    RUN_TEST(test_soft_reports_a_zero_floor);
    RUN_TEST(test_soft_reports_the_top_floor);
    RUN_TEST(test_soft_does_not_touch_the_element);
    RUN_TEST(test_element_reports_the_boot_counter);
    RUN_TEST(test_element_reports_a_zero_counter);
    RUN_TEST(test_element_reports_every_counter_byte);
    RUN_TEST(test_element_ignores_the_platform_floor);
    RUN_TEST(test_element_without_a_part_reports_no_floor);
    RUN_TEST(test_element_refused_read_reports_no_floor);
    RUN_TEST(test_element_sleeps_after_the_read);
    return UNITY_END();
}
