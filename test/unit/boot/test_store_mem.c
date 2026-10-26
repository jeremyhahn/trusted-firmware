/*
 * The memory image store (OTA_STORE := mem) from both of its sides: the
 * application's driver, src/store/mem.c, and tfw-boot's, which answers
 * boot/boot_stage.c's SPI NOR opcodes over the same span
 * (boot/hal/cortex-m/boot_ota_store_cortex-m.c). Against a host array the
 * size of three sectors (test/model/store_mem_model.h).
 *
 * The NOR rules are the point: an erase is a whole sector to 0xFF, a write
 * or a program only clears bits, a program without a write enable does
 * nothing, and the boot side reads back what the application side staged.
 */
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/status.h"
#include "store_mem_model.h"
#include "ota_store.h"
#include "boot_hal.h"

uint8_t tfw_store_mem_model[TFW_OTA_STORE_MEM_SIZE];

#define SECTOR ((uint32_t)TFW_STORE_SECTOR)

void setUp(void)
{
    memset(tfw_store_mem_model, 0x5A, sizeof tfw_store_mem_model);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_open());
}
void tearDown(void) { tfw_boot_store_close(); }

static uint8_t all(uint32_t from, uint32_t len, uint8_t v)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (tfw_store_mem_model[from + i] != v) {
            return 0u;
        }
    }
    return 1u;
}

static void test_the_store_is_always_present(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_probe());
}

static void test_an_erase_is_the_whole_sector_holding_the_address_and_no_more(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(SECTOR + 17u));
    TEST_ASSERT_TRUE(all(0u, SECTOR, 0x5Au));
    TEST_ASSERT_TRUE(all(SECTOR, SECTOR, 0xFFu));
    TEST_ASSERT_TRUE(all(2u * SECTOR, SECTOR, 0x5Au));
}

static void test_an_erase_past_the_span_is_refused_and_touches_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_erase(TFW_OTA_STORE_MEM_SIZE));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_erase(0xFFFFFFFFul));
    TEST_ASSERT_TRUE(all(0u, TFW_OTA_STORE_MEM_SIZE, 0x5Au));
}

static void test_a_write_only_clears_bits(void)
{
    const uint8_t a[2] = { 0xF0u, 0x0Fu };
    const uint8_t b[2] = { 0x3Cu, 0xFFu };
    uint8_t       out[2];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(10u, a, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(10u, b, 2u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_read(10u, out, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x30u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0Fu, out[1]);
}

static void test_reads_and_writes_longer_than_a_chunk_are_refused(void)
{
    uint8_t buf[TFW_STORE_CHUNK + 1u];

    memset(buf, 0, sizeof buf);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_read(0u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_write(0u, buf, (uint8_t)sizeof buf));
    TEST_ASSERT_TRUE(all(0u, TFW_OTA_STORE_MEM_SIZE, 0x5Au));
}

static void test_the_last_bytes_of_the_span_are_reachable_and_one_past_is_not(void)
{
    const uint8_t z[4] = { 0u, 0u, 0u, 0u };
    uint8_t       out[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(TFW_OTA_STORE_MEM_SIZE - 4u, z, 4u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_read(TFW_OTA_STORE_MEM_SIZE - 4u, out, 4u));
    TEST_ASSERT_EQUAL_MEMORY(z, out, 4u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_read(TFW_OTA_STORE_MEM_SIZE - 3u, out, 4u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_write(TFW_OTA_STORE_MEM_SIZE - 3u, z, 4u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_store_read(0xFFFFFFFEul, out, 4u));
}

static void test_boot_reads_back_what_the_application_staged(void)
{
    uint8_t chunk[TFW_STORE_CHUNK];
    uint8_t cmd[4] = { 0x03u, 0x00u, 0x10u, 0x20u };    /* read at 0x001020 */
    uint8_t out[TFW_STORE_CHUNK];
    uint8_t i;

    for (i = 0u; i < TFW_STORE_CHUNK; i++) {
        chunk[i] = (uint8_t)(i * 7u + 1u);
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_erase(0x1020u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_store_write(0x1020u, chunk, TFW_STORE_CHUNK));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(cmd, 4u, out, TFW_STORE_CHUNK));
    TEST_ASSERT_EQUAL_MEMORY(chunk, out, TFW_STORE_CHUNK);
}

static void test_boot_answers_a_jedec_id_the_stage_take_accepts(void)
{
    uint8_t op = 0x9Fu;
    uint8_t id[3];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(&op, 1u, id, 3u));
    TEST_ASSERT_TRUE(id[0] != 0x00u && id[0] != 0xFFu);
}

static void test_boot_status_is_never_busy(void)
{
    uint8_t op = 0x05u;
    uint8_t sr = 0xFFu;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(&op, 1u, &sr, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, sr);
}

static void test_boot_program_needs_a_write_enable_each_time_and_only_clears_bits(void)
{
    uint8_t wren = 0x06u;
    uint8_t prog[6] = { 0x02u, 0x00u, 0x00u, 0x08u, 0x0Fu, 0xF0u };

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(prog, 6u, prog, 0u));
    TEST_ASSERT_TRUE(all(0u, TFW_OTA_STORE_MEM_SIZE, 0x5Au));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(&wren, 1u, &wren, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(prog, 6u, prog, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x0Au, tfw_store_mem_model[8]);
    TEST_ASSERT_EQUAL_HEX8(0x50u, tfw_store_mem_model[9]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, tfw_store_mem_model[10]);

    /* The latch is spent: a second program is refused until the next WREN. */
    prog[4] = 0x00u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(prog, 6u, prog, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x0Au, tfw_store_mem_model[8]);
}

static void test_boot_refuses_what_it_does_not_model(void)
{
    uint8_t wren    = 0x06u;
    uint8_t erase[4] = { 0x20u, 0x00u, 0x00u, 0x00u };
    uint8_t read[4]  = { 0x03u, 0x00u, 0x2Fu, 0xFDu };  /* 4 bytes from 3 before the end */
    uint8_t out[4];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_store_cmd(&wren, 1u, &wren, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(erase, 4u, erase, 0u));
    TEST_ASSERT_TRUE(all(0u, TFW_OTA_STORE_MEM_SIZE, 0x5Au));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(read, 4u, out, 4u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(read, 3u, out, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_store_cmd(read, 0u, out, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_store_is_always_present);
    RUN_TEST(test_an_erase_is_the_whole_sector_holding_the_address_and_no_more);
    RUN_TEST(test_an_erase_past_the_span_is_refused_and_touches_nothing);
    RUN_TEST(test_a_write_only_clears_bits);
    RUN_TEST(test_reads_and_writes_longer_than_a_chunk_are_refused);
    RUN_TEST(test_the_last_bytes_of_the_span_are_reachable_and_one_past_is_not);
    RUN_TEST(test_boot_reads_back_what_the_application_staged);
    RUN_TEST(test_boot_answers_a_jedec_id_the_stage_take_accepts);
    RUN_TEST(test_boot_status_is_never_busy);
    RUN_TEST(test_boot_program_needs_a_write_enable_each_time_and_only_clears_bits);
    RUN_TEST(test_boot_refuses_what_it_does_not_model);
    return UNITY_END();
}
