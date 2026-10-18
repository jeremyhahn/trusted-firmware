/*
 * The SPI NOR store driver (src/store/spi_nor.c, issue #75) against the
 * software part: the chunk limit, the page split and the bus-error paths.
 *
 * tfw_store_init's TFW_ERR_UNSUPPORTED (spi_nor.c:53) is not here: the select
 * comes from the board profile through tfw_board_pin, and the mock has no way
 * to make a board's own pin unresolvable.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/status.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "spi_nor_hal.h"
#include "ota_store.h"

#define BUS_ERR TFW_ERR_NACK    /* any status the driver itself never makes */

void setUp(void)
{
    tfw_nor_model_reset();
    tfw_nor_shim_reset();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
}
void tearDown(void) {}

static void test_read_and_write_longer_than_a_chunk_are_refused(void)
{
    uint8_t buf[TFW_STORE_CHUNK + 1u];

    memset(buf, 0x5A, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_read(0u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_write(0u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_shim_xfers());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

static void test_write_across_a_page_is_split_there(void)
{
    uint8_t src[16], back[16];
    uint8_t i;

    for (i = 0u; i < sizeof src; i++) {
        src[i] = (uint8_t)(0xA0u + i);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(250u, src, (uint8_t)sizeof src));
    TEST_ASSERT_EQUAL_UINT32(2u, tfw_nor_model_programs());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_violations());
    TEST_ASSERT_EQUAL_MEMORY(src, &tfw_nor_model_mem()[250], sizeof src);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_read(250u, back, (uint8_t)sizeof back));
    TEST_ASSERT_EQUAL_MEMORY(src, back, sizeof src);
}

/* Each write is WREN, PROGRAM, then STATUS polls; an erase the same with ERASE. */
static void test_a_bus_error_in_write_enable_is_passed_on(void)
{
    uint8_t b = 0u;

    tfw_nor_shim_fail(1u, BUS_ERR);
    TEST_ASSERT_EQUAL(BUS_ERR, tfw_store_write(0u, &b, 1u));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nor_shim_xfers());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

static void test_a_bus_error_in_program_is_passed_on(void)
{
    uint8_t b = 0u;

    tfw_nor_shim_fail(2u, BUS_ERR);
    TEST_ASSERT_EQUAL(BUS_ERR, tfw_store_write(0u, &b, 1u));
    TEST_ASSERT_EQUAL_UINT32(2u, tfw_nor_shim_xfers());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_programs());
}

static void test_a_bus_error_in_wait_ready_is_passed_on(void)
{
    uint8_t b = 0u;

    tfw_nor_shim_fail(3u, BUS_ERR);
    TEST_ASSERT_EQUAL(BUS_ERR, tfw_store_write(0u, &b, 1u));
    TEST_ASSERT_EQUAL_UINT32(3u, tfw_nor_shim_xfers());
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nor_model_programs());
}

static void test_a_bus_error_in_erase_is_passed_on(void)
{
    tfw_nor_shim_fail(2u, BUS_ERR);
    TEST_ASSERT_EQUAL(BUS_ERR, tfw_store_erase(0u));
    TEST_ASSERT_EQUAL_UINT32(2u, tfw_nor_shim_xfers());
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_nor_model_erases());
}

static void test_a_bus_error_in_read_is_passed_on(void)
{
    uint8_t buf[4];

    memset(buf, 0x5A, sizeof buf);
    tfw_nor_shim_fail(1u, BUS_ERR);
    TEST_ASSERT_EQUAL(BUS_ERR, tfw_store_read(0u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL_UINT32(1u, tfw_nor_shim_xfers());
    TEST_ASSERT_EACH_EQUAL_UINT8(0x5Au, buf, sizeof buf);   /* the caller's buffer is untouched */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_and_write_longer_than_a_chunk_are_refused);
    RUN_TEST(test_write_across_a_page_is_split_there);
    RUN_TEST(test_a_bus_error_in_write_enable_is_passed_on);
    RUN_TEST(test_a_bus_error_in_program_is_passed_on);
    RUN_TEST(test_a_bus_error_in_wait_ready_is_passed_on);
    RUN_TEST(test_a_bus_error_in_erase_is_passed_on);
    RUN_TEST(test_a_bus_error_in_read_is_passed_on);
    return UNITY_END();
}
