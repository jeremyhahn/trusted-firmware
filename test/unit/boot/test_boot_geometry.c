/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The flash geometry and session limits, pinned as literals.
 *
 * The region base, the region size and the OTA page buffer now come from the
 * board profile, not from arithmetic on the top of flash. That change is meant
 * to leave every AVR image the same, so the numbers the old arithmetic gave are
 * written out here. They are literals on purpose. A value worked out from the
 * same macros would follow the macros wherever they went.
 *
 * Only the row for the board the host build is configured for runs. The other
 * rows are written down so the next board's numbers have a place to go, and a
 * build for a board with no row fails rather than passes.
 */

#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_hal.h"
#include "boot_ota.h"

/* The session limits are pinned at the 16 MHz every AVR board ships at. The tick
 * header takes F_CPU from the compiler on the target; the host build has none, so
 * mk/boot.mk gives BOOT_TEST_CFLAGS the same 16 MHz. The clock is a build input,
 * and a test outside the AVR directories does not get to name the register-level
 * macro (make lint-arch-leak). */
#include "hal/avr/boot_tick.h"

typedef struct {
    uint8_t  board_id;
    uint32_t app_base;
    uint32_t app_end;
    uint32_t trailer_addr;
    uint32_t max_image_len;
    uint32_t boot_start;
    uint32_t boot_size;
    uint32_t ota_page_max;
} geometry_t;

static const geometry_t rows[] = {
    /* id     app base  app end   trailer   max image  boot start boot size  page */
    { 0x01u, 0u,       28672u,   28544u,   28544u,    0x7000u,   4096u,     256u }, /* 328P */
    { 0x02u, 0u,       65536u,   65408u,   65408u,    0x3E000u,  8192u,     256u }, /* 2560: the A/B primary */
    { 0x03u, 0u,       28672u,   28544u,   28544u,    0x7000u,   4096u,     256u }, /* 32U4 */
    { 0x04u, 0u,       8192u,    8064u,    8064u,     8192u,     0u,        256u }, /* tiny85 */
};

static const geometry_t *this_board(void)
{
    for (size_t i = 0u; i < sizeof rows / sizeof rows[0]; i++) {
        if (rows[i].board_id == (uint8_t)TFW_BOARD_ID) {
            return &rows[i];
        }
    }
    return 0;
}

void setUp(void)    { tfw_boot_mock_reset(); }
void tearDown(void) {}

static void test_the_board_has_a_geometry_row(void)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(this_board(),
        "no geometry row for TFW_BOARD_ID: add one with the board's numbers");
}

static void test_the_application_region_is_the_board_profile_one(void)
{
    const geometry_t *g = this_board();

    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT32(g->app_base, TFW_BOOT_APP_BASE);
    TEST_ASSERT_EQUAL_UINT32(g->app_end, TFW_BOOT_APP_END);
    TEST_ASSERT_EQUAL_UINT32(g->trailer_addr, TFW_BOOT_TRAILER_ADDR);
    TEST_ASSERT_EQUAL_UINT32(g->max_image_len, TFW_BOOT_MAX_IMAGE_LEN);
}

static void test_the_boot_region_is_the_board_profile_one(void)
{
    const geometry_t *g = this_board();

    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT32(g->boot_start, TFW_BOOT_START);
    TEST_ASSERT_EQUAL_UINT32(g->boot_size, (uint32_t)TFW_BOOT_REGION_SIZE);
}

/* TCG DICE HW Req 6.4, the part of the MPU clause a host build can reach. BLB1
 * mode 3 guards the boot section, the top TFW_BOOT_REGION_SIZE bytes of flash,
 * against the application; it does not stop tfw-boot writing over itself, so
 * the highest address tfw-boot programs must end below it. Whether the lock
 * byte is set, and what LPM from the application then reads, is silicon's. */
static void test_tfw_boot_writes_nothing_in_the_boot_section(void)
{
    TEST_ASSERT_EQUAL_UINT32((uint32_t)TFW_FLASH_TOTAL, TFW_BOOT_START + (uint32_t)TFW_BOOT_REGION_SIZE);
    TEST_ASSERT_TRUE((uint32_t)TFW_APP_WRITE_END <= TFW_BOOT_START);
}

static void test_the_ota_page_buffer_is_the_board_profile_one(void)
{
    const geometry_t *g = this_board();

    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT32(g->ota_page_max, (uint32_t)TFW_OTA_PAGE_MAX);
}

static tfw_status_t no_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr;
    (void)data;
    (void)len;
    return TFW_OK;
}

/* The mock's page is the 328P's 128 bytes. A buffer the board sized must hold
 * it, or OTA would refuse to start on the part it was built for. */
static void test_ota_accepts_the_part_page_within_the_buffer(void)
{
    tfw_ota_t o;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&o, no_write, tfw_boot_flash_page_size()));
}

/* 16 MHz / 77 cycles per empty poll, truncated: the tick tfw-boot counts. */
static void test_the_session_tick_is_one_empty_poll(void)
{
    TEST_ASSERT_EQUAL_UINT32(77u, (uint32_t)TFW_BOOT_POLL_CYCLES);
    TEST_ASSERT_EQUAL_UINT32(207792u, (uint32_t)TFW_BOOT_TICK_HZ);
}

/* The limits the cycle-count arithmetic gave at 16 MHz, before the tick. */
static void test_the_session_limits_are_unchanged_at_16_mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(2077920u, TFW_BOOT_SESSION_IDLE_POLLS(TFW_BOOT_TICK_HZ));
    TEST_ASSERT_EQUAL_UINT32(12467520u, TFW_BOOT_SESSION_MAX_POLLS(TFW_BOOT_TICK_HZ));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_has_a_geometry_row);
    RUN_TEST(test_the_application_region_is_the_board_profile_one);
    RUN_TEST(test_the_boot_region_is_the_board_profile_one);
    RUN_TEST(test_tfw_boot_writes_nothing_in_the_boot_section);
    RUN_TEST(test_the_ota_page_buffer_is_the_board_profile_one);
    RUN_TEST(test_ota_accepts_the_part_page_within_the_buffer);
    RUN_TEST(test_the_session_tick_is_one_empty_poll);
    RUN_TEST(test_the_session_limits_are_unchanged_at_16_mhz);
    return UNITY_END();
}
