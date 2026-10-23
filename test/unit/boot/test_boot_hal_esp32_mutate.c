/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/esp32/boot_hal_esp32.c, the boot HAL on ESP-IDF's second-stage
 * bootloader (#48, #163), against host stand-ins for the ESP-IDF calls it
 * makes (test/unit/boot/boot_hal_esp32/). The real flash_span.c and
 * l0_gate.c are linked beside it, as the bootloader links them.
 *
 * The flash stand-in keeps bootloader_flash_read's and _write's contract: a
 * source, length or destination not 4-byte aligned is refused. Every test is
 * written against a mutant of the unit: a comparison flipped, a bound moved by
 * one, a check or a call dropped, an error path swapped.
 *
 * Nothing is fused. esp_flash_encryption_enabled, the one eFuse read, answers
 * the unfused value unless a test sets it, and nothing writes an eFuse.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "bootloader_common.h"
#include "bootloader_flash_priv.h"
#include "bootloader_utility.h"
#include "esp_flash_encrypt.h"
#include "esp_flash_partitions.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"

#include "boot_hal.h"
#include "boot_image.h"
#include "l0_gate.h"
#include "tfw/boot.h"

/* ---- the stand-ins ---------------------------------------------------- */

#define FLASH_SIZE 0x20000u
#define SECTOR     4096u
#define PLAT_OFF   0x10000u
#define AUTH_OFF   0x14000u
#define TABLE_END  (ESP_PARTITION_TABLE_OFFSET + ESP_PARTITION_TABLE_MAX_LEN)

static union {
    uint32_t words[FLASH_SIZE / 4u];
    uint8_t  bytes[FLASH_SIZE];
} flash;

static bool     encrypted;              /* the eFuse: unfused unless a test sets it */
static int      read_fail_at;           /* the Nth read and every one after fail; 0 never */
static int      reads;
static int      reads_plain;            /* reads made without decryption */
static int      reads_decrypt;
static bool     erase_fails;
static bool     write_fails;
static size_t   erased_sector;
static int      erases;
static int      writes;
static size_t   written_at;
static bool     written_encrypted;
static bool     mmap_fails;
static int      maps;
static int      unmaps;
static soc_reset_reason_t reason;
static int      reason_cpu;
static int      rx_char;                /* -1: nothing waiting */
static uint8_t  tx_last;
static int      txs;
static rtc_retain_mem_t retain;
static bool     table_loads;
static int      selected;
static int      loaded_index;
static int      loads;
static int      resets;
static int      calls;                  /* order: each loader call takes the next number */
static int      reset_call;
static int      load_call;

bool esp_flash_encryption_enabled(void)
{
    return encrypted;
}

static bool aligned(size_t v)
{
    return (v & 3u) == 0u;
}

esp_err_t bootloader_flash_read(size_t src_addr, void *dest, size_t size, bool allow_decrypt)
{
    reads++;
    if (allow_decrypt) {
        reads_decrypt++;
    } else {
        reads_plain++;
    }
    if (read_fail_at != 0 && reads >= read_fail_at) {
        return ESP_FAIL;
    }
    if (!aligned(src_addr) || !aligned(size) || !aligned((size_t)(uintptr_t)dest)
        || src_addr + size > FLASH_SIZE) {
        return ESP_FAIL;
    }
    memcpy(dest, &flash.bytes[src_addr], size);
    return ESP_OK;
}

esp_err_t bootloader_flash_write(size_t dest_addr, void *src, size_t size, bool write_encrypted)
{
    size_t i;

    writes++;
    written_at        = dest_addr;
    written_encrypted = write_encrypted;
    if (write_fails) {
        return ESP_FAIL;
    }
    if (!aligned(dest_addr) || !aligned(size) || !aligned((size_t)(uintptr_t)src)
        || dest_addr + size > FLASH_SIZE) {
        return ESP_FAIL;
    }
    for (i = 0; i < size; i++) {         /* NOR: a write only clears bits */
        flash.bytes[dest_addr + i] &= ((const uint8_t *)src)[i];
    }
    return ESP_OK;
}

esp_err_t bootloader_flash_erase_sector(size_t sector)
{
    erases++;
    erased_sector = sector;
    if (erase_fails) {
        return ESP_FAIL;
    }
    if ((sector + 1u) * SECTOR > FLASH_SIZE) {
        return ESP_FAIL;
    }
    memset(&flash.bytes[sector * SECTOR], 0xFF, SECTOR);
    return ESP_OK;
}

const void *bootloader_mmap(uint32_t src_addr, uint32_t size)
{
    if (mmap_fails || src_addr + size > FLASH_SIZE) {
        return NULL;
    }
    maps++;
    return &flash.bytes[src_addr];
}

void bootloader_munmap(const void *mapping)
{
    TEST_ASSERT_EQUAL_PTR(&flash.bytes[ESP_PARTITION_TABLE_OFFSET], mapping);
    unmaps++;
}

soc_reset_reason_t esp_rom_get_reset_reason(int cpu_no)
{
    reason_cpu = cpu_no;
    return reason;
}

int esp_rom_output_rx_one_char(uint8_t *c)
{
    if (rx_char < 0) {
        return -1;
    }
    *c      = (uint8_t)rx_char;
    rx_char = -1;
    return 0;
}

int esp_rom_output_tx_one_char(uint8_t c)
{
    tx_last = c;
    txs++;
    return 0;
}

rtc_retain_mem_t *bootloader_common_get_rtc_retain_mem(void)
{
    return &retain;
}

bool bootloader_utility_load_partition_table(bootloader_state_t *bs)
{
    TEST_ASSERT_NOT_NULL(bs);
    return table_loads;
}

int bootloader_utility_get_selected_boot_partition(const bootloader_state_t *bs)
{
    TEST_ASSERT_NOT_NULL(bs);
    return selected;
}

void bootloader_utility_load_boot_image(const bootloader_state_t *bs, int start_index)
{
    TEST_ASSERT_NOT_NULL(bs);
    loaded_index = start_index;
    loads++;
    load_call = ++calls;
}

void bootloader_reset(void)
{
    resets++;
    reset_call = ++calls;
}

/* ---- the partition table ---------------------------------------------- */

static int entries;

static void add_partition(const char *label, uint32_t offset, uint32_t size)
{
    esp_partition_info_t e;

    memset(&e, 0, sizeof e);
    e.magic      = ESP_PARTITION_MAGIC;
    e.type       = 1u;                  /* data */
    e.pos.offset = offset;
    e.pos.size   = size;
    memcpy(e.label, label, strlen(label) < sizeof e.label ? strlen(label) : sizeof e.label);
    memcpy(&flash.bytes[ESP_PARTITION_TABLE_OFFSET + (uint32_t)entries * sizeof e], &e, sizeof e);
    entries++;
}

static void fill(uint32_t addr, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        flash.bytes[addr + i] = (uint8_t)(addr + i * 7u + 1u);
    }
}

void setUp(void)
{
    memset(flash.bytes, 0xFF, sizeof flash.bytes);
    memset(&retain, 0, sizeof retain);
    encrypted = false;
    read_fail_at = reads = reads_plain = reads_decrypt = 0;
    erase_fails = write_fails = mmap_fails = false;
    erased_sector = written_at = 0;
    erases = writes = maps = unmaps = 0;
    written_encrypted = false;
    reason = RESET_REASON_CHIP_POWER_ON;
    reason_cpu = -1;
    rx_char = -1;
    tx_last = 0;
    txs = 0;
    table_loads = true;
    selected = 0;
    loaded_index = -1;
    loads = resets = calls = reset_call = load_call = 0;
    entries = 0;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(maps, unmaps);  /* every mapping is released */
}

/* ---- flash ------------------------------------------------------------- */

static void test_flash_read_returns_the_bytes_at_any_offset_and_length(void)
{
    uint8_t  buf[256];
    uint32_t addr;
    uint16_t len;

    fill(0x3000u, 0x400u);
    for (addr = 0x3000u; addr < 0x3008u; addr++) {
        for (len = 1u; len <= 255u; len = (uint16_t)(len + (len < 9u ? 1u : 37u))) {
            memset(buf, 0xA5, sizeof buf);
            tfw_boot_flash_read(addr, buf, (uint8_t)len);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[addr], buf, len);
            TEST_ASSERT_EQUAL_HEX8(0xA5, buf[len]);     /* nothing past len */
        }
    }
    TEST_ASSERT_EQUAL_INT(0, reads_plain);              /* it decrypts */
}

static void test_flash_read_of_255_unaligned_bytes(void)
{
    uint8_t buf[255];

    fill(0x4000u, 0x200u);
    tfw_boot_flash_read(0x40FFu, buf, 255u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[0x40FFu], buf, 255u);
}

static void test_a_failed_flash_read_reads_as_erased(void)
{
    uint8_t buf[40];

    fill(0x3000u, 0x100u);
    memset(buf, 0, sizeof buf);
    read_fail_at = 1;
    tfw_boot_flash_read(0x3001u, buf, 39u);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, buf, 39u);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[39]);
}

static void test_a_flash_read_failing_on_a_later_piece_reads_as_erased(void)
{
    uint8_t buf[255];

    fill(0x3000u, 0x200u);
    memset(buf, 0, sizeof buf);
    read_fail_at = 2;                   /* 0x30F3 + 255 spans two windows */
    tfw_boot_flash_read(0x30F3u, buf, 255u);
    TEST_ASSERT_TRUE(reads >= 2);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xFF, buf, 255u);
}

static void test_flash_read_of_nothing_reads_nothing(void)
{
    uint8_t buf[1] = { 0x5A };

    tfw_boot_flash_read(0x3000u, buf, 0u);
    TEST_ASSERT_EQUAL_INT(0, reads);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[0]);
}

static void test_the_page_is_the_4k_sector(void)
{
    TEST_ASSERT_EQUAL_UINT16(4096u, tfw_boot_flash_page_size());
}

static uint8_t page_src[SECTOR + 1u];

static void test_a_page_write_erases_then_writes_its_sector(void)
{
    uint32_t i;

    for (i = 0; i < SECTOR; i++) {
        page_src[i + 1u] = (uint8_t)(i * 13u + 5u);
    }
    memset(&flash.bytes[0x6000u], 0x00, SECTOR);        /* not erased */
    /* An unaligned source: the unit copies it into words first. */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_flash_write_page(0x6000u, &page_src[1]));
    TEST_ASSERT_EQUAL_UINT(6u, erased_sector);
    TEST_ASSERT_EQUAL_UINT(0x6000u, written_at);
    TEST_ASSERT_FALSE(written_encrypted);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&page_src[1], &flash.bytes[0x6000u], SECTOR);
    TEST_ASSERT_EQUAL_HEX8(0xFF, flash.bytes[0x7000u]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, flash.bytes[0x5FFFu]);
}

static void test_a_page_write_encrypts_when_flash_encryption_is_on(void)
{
    encrypted = true;
    memset(page_src, 0x11, sizeof page_src);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_flash_write_page(0x0u, page_src));
    TEST_ASSERT_TRUE(written_encrypted);
    TEST_ASSERT_EQUAL_UINT(0u, erased_sector);
}

static void test_a_page_write_off_a_sector_boundary_is_refused(void)
{
    memset(page_src, 0, sizeof page_src);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_flash_write_page(0x6001u, page_src));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_flash_write_page(0x6FFFu, page_src));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_flash_write_page(0x6004u, page_src));
    TEST_ASSERT_EQUAL_INT(0, erases);
    TEST_ASSERT_EQUAL_INT(0, writes);
}

static void test_a_failed_erase_writes_nothing(void)
{
    memset(page_src, 0, sizeof page_src);
    erase_fails = true;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_flash_write_page(0x6000u, page_src));
    TEST_ASSERT_EQUAL_INT(0, writes);
}

static void test_a_failed_write_is_internal(void)
{
    memset(page_src, 0, sizeof page_src);
    write_fails = true;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_flash_write_page(0x6000u, page_src));
    TEST_ASSERT_EQUAL_INT(1, writes);
}

/* ---- the platform block ----------------------------------------------- */

static void test_nvm_read_reads_the_tfw_plat_partition(void)
{
    uint8_t buf[200];

    add_partition("nvs", 0x9000u, 0x4000u);
    add_partition("tfw_plat_old", 0xC000u, 0x1000u);    /* a longer label is another */
    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    fill(0xC000u, 0x400u);
    fill(PLAT_OFF, 0x2000u);
    flash.bytes[PLAT_OFF + 7u] ^= 0x5Au;                /* differs from 0xC007 */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_read(3u, buf, 200u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[PLAT_OFF + 3u], buf, 200u);
    TEST_ASSERT_EQUAL_INT(0, reads_plain);              /* it decrypts */
    TEST_ASSERT_EQUAL_INT(1, maps);
}

static void test_nvm_read_without_the_partition_is_unsupported(void)
{
    uint8_t buf[4];

    add_partition("nvs", 0x9000u, 0x4000u);
    add_partition("tfw_pla", PLAT_OFF, 0x2000u);        /* a shorter label is another */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL_INT(0, reads);
}

/* The table is read in order and the first entry with the label is the one:
 * a later entry of the same name is never reached. */
static void test_the_first_entry_with_the_label_is_used(void)
{
    uint8_t buf[4];

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    add_partition("tfw_plat", 0xC000u, 0x2000u);
    fill(PLAT_OFF, 4u);
    fill(0xC000u, 4u);
    flash.bytes[0xC000u] ^= 0xFFu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[PLAT_OFF], buf, 4u);
}

static void test_the_table_ends_at_the_first_entry_without_magic(void)
{
    uint8_t buf[4];

    add_partition("nvs", 0x9000u, 0x4000u);
    entries++;                          /* an erased entry */
    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_read(0u, buf, 4u));
}

static void test_the_last_entry_of_a_full_table_is_found(void)
{
    uint8_t buf[4];
    int     n = (int)(ESP_PARTITION_TABLE_MAX_LEN / sizeof(esp_partition_info_t));

    while (entries < n - 1) {
        add_partition("x", 0x9000u, 0x1000u);
    }
    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    fill(PLAT_OFF, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[PLAT_OFF], buf, 4u);
}

static void test_nothing_past_the_table_is_read_as_an_entry(void)
{
    uint8_t buf[4];
    int     n = (int)(ESP_PARTITION_TABLE_MAX_LEN / sizeof(esp_partition_info_t));

    while (entries < n) {
        add_partition("x", 0x9000u, 0x1000u);
    }
    add_partition("tfw_plat", PLAT_OFF, 0x2000u);       /* just past MAX_LEN */
    TEST_ASSERT_EQUAL_UINT32(TABLE_END + sizeof(esp_partition_info_t),
                             ESP_PARTITION_TABLE_OFFSET + (uint32_t)entries * sizeof(esp_partition_info_t));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_read(0u, buf, 4u));
}

static void test_an_unmappable_table_is_unsupported(void)
{
    uint8_t buf[TFW_BOOT_AUTH_LEN];

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    mmap_fails = true;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_read(0u, buf, 4u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_write(0u, buf, 4u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_l0_auth_read(buf));
}

static void test_nvm_read_stays_inside_the_partition(void)
{
    uint8_t buf[16];

    add_partition("tfw_plat", PLAT_OFF, 0x100u);
    fill(PLAT_OFF, 0x110u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_read(0xF0u, buf, 16u));      /* ends at size */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[PLAT_OFF + 0xF0u], buf, 16u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_read(0xF1u, buf, 16u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_read(0xFFFFu, buf, 16u));
}

static void test_a_failed_nvm_read_is_internal(void)
{
    uint8_t buf[16];

    add_partition("tfw_plat", PLAT_OFF, 0x100u);
    read_fail_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_nvm_read(0u, buf, 16u));
}

static void test_nvm_write_rewrites_its_bytes_and_keeps_the_rest(void)
{
    uint8_t  sector[SECTOR];
    uint8_t  data[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    fill(PLAT_OFF, 0x2000u);
    memcpy(sector, &flash.bytes[PLAT_OFF], SECTOR);
    memcpy(&sector[0x101u], data, sizeof data);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_write(0x101u, data, (uint8_t)sizeof data));
    TEST_ASSERT_EQUAL_UINT(PLAT_OFF / SECTOR, erased_sector);
    TEST_ASSERT_EQUAL_UINT(PLAT_OFF, written_at);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sector, &flash.bytes[PLAT_OFF], SECTOR);
    TEST_ASSERT_EQUAL_INT(0, reads_plain);
}

static void test_nvm_write_to_the_sectors_last_byte(void)
{
    uint8_t b = 0x42;

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_write(SECTOR - 1u, &b, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x42, flash.bytes[PLAT_OFF + SECTOR - 1u]);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write(SECTOR, &b, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write(SECTOR - 4u, (const uint8_t *)"abcde", 5u));
    TEST_ASSERT_EQUAL_INT(1, writes);
}

static void test_nvm_write_stays_inside_a_small_partition(void)
{
    uint8_t d[8] = { 0 };

    add_partition("tfw_plat", PLAT_OFF, 0x40u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_nvm_write(0x38u, d, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_boot_nvm_write(0x39u, d, 8u));
    TEST_ASSERT_EQUAL_INT(1, writes);
}

static void test_nvm_write_without_the_partition_is_unsupported(void)
{
    uint8_t d[1] = { 0 };

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_nvm_write(0u, d, 1u));
    TEST_ASSERT_EQUAL_INT(0, erases);
}

static void test_a_failed_sector_read_writes_nothing(void)
{
    uint8_t d[1] = { 0 };

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    read_fail_at = 3;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_nvm_write(0u, d, 1u));
    TEST_ASSERT_EQUAL_INT(0, erases);
    TEST_ASSERT_EQUAL_INT(0, writes);
}

static void test_nvm_write_passes_on_the_page_writes_status(void)
{
    uint8_t d[1] = { 0 };

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    write_fails = true;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_boot_nvm_write(0u, d, 1u));
}

/* ---- the activate authorization --------------------------------------- */

static void test_auth_read_reads_the_record_as_written(void)
{
    uint8_t rec[TFW_BOOT_AUTH_LEN + 1u];

    add_partition("tfw_plat", PLAT_OFF, 0x2000u);
    add_partition("tfw_auth", AUTH_OFF, TFW_BOOT_AUTH_LEN);  /* exactly a record */
    fill(AUTH_OFF, 0x100u);
    rec[TFW_BOOT_AUTH_LEN] = 0x77u;
    encrypted = true;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_l0_auth_read(rec));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&flash.bytes[AUTH_OFF], rec, TFW_BOOT_AUTH_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x77u, rec[TFW_BOOT_AUTH_LEN]);
    TEST_ASSERT_EQUAL_INT(0, reads_decrypt);            /* not a secret */
}

static void test_auth_read_without_the_partition_needs_authorization(void)
{
    uint8_t rec[TFW_BOOT_AUTH_LEN];

    add_partition("tfw_plat", AUTH_OFF, 0x1000u);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_l0_auth_read(rec));
    TEST_ASSERT_EQUAL_INT(0, reads);
}

static void test_auth_read_of_a_partition_smaller_than_a_record(void)
{
    uint8_t rec[TFW_BOOT_AUTH_LEN];

    add_partition("tfw_auth", AUTH_OFF, TFW_BOOT_AUTH_LEN - 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_l0_auth_read(rec));
    TEST_ASSERT_EQUAL_INT(0, reads);
}

static void test_a_failed_auth_read_is_internal(void)
{
    uint8_t rec[TFW_BOOT_AUTH_LEN];

    add_partition("tfw_auth", AUTH_OFF, 0x1000u);
    read_fail_at = 1;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_l0_auth_read(rec));
}

/* ---- l0_gate.c over this HAL ------------------------------------------ */

/* The gate reads its trailer through tfw_boot_flash_read, after the image at
 * an offset nothing aligns. Erased, it refuses as the trailer parse refuses
 * erased bytes; a failed read is the same, since it reads as erased. */
static void test_the_gate_reads_its_trailer_through_this_hal(void)
{
    uint8_t            ff[TFW_BOOT_TRAILER_LEN];
    uint8_t            digest[TFW_BOOT_MEASURE_LEN] = { 0 };
    tfw_boot_trailer_t tr;
    tfw_status_t       want;

    memset(ff, 0xFF, sizeof ff);
    want = tfw_boot_trailer_parse(ff, &tr);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, want);
    TEST_ASSERT_EQUAL(want, tfw_l0_gate(0x10000u, 0x10000u, 0x1235u, digest, 0u, &tr));
    TEST_ASSERT_TRUE(reads >= 2);

    fill(0x11235u, TFW_BOOT_TRAILER_LEN);
    read_fail_at = reads + 1;
    TEST_ASSERT_EQUAL(want, tfw_l0_gate(0x10000u, 0x10000u, 0x1235u, digest, 0u, &tr));
}

/* No record: an erased tfw_auth partition authorizes nothing. */
static void test_an_erased_auth_record_authorizes_nothing(void)
{
    uint8_t            rec[TFW_BOOT_AUTH_LEN];
    tfw_boot_trailer_t tr;

    memset(&tr, 0, sizeof tr);
    add_partition("tfw_auth", AUTH_OFF, 0x1000u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_l0_auth_read(rec));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_l0_authorized(&tr, rec));
}

/* ---- uart ------------------------------------------------------------- */

static void test_uart_rx_reports_a_waiting_byte(void)
{
    uint8_t b = 0;

    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    rx_char = 0xC3;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xC3, b);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
}

static void test_uart_tx_sends_the_byte(void)
{
    tfw_boot_uart_tx(0x7Eu);
    TEST_ASSERT_EQUAL_INT(1, txs);
    TEST_ASSERT_EQUAL_HEX8(0x7E, tx_last);
}

/* ---- handoff ------------------------------------------------------------ */

static void test_handoff_round_trips_through_the_retained_record(void)
{
    uint8_t in[TFW_BOOT_HANDOFF_LEN];
    uint8_t out[TFW_BOOT_HANDOFF_LEN];
    uint8_t i;

    for (i = 0; i < TFW_BOOT_HANDOFF_LEN; i++) {
        in[i] = (uint8_t)(i * 3u + 1u);
    }
    tfw_boot_handoff_write(in, TFW_BOOT_HANDOFF_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, retain.custom, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_read(out, TFW_BOOT_HANDOFF_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, TFW_BOOT_HANDOFF_LEN);
}

static void test_handoff_copies_no_more_than_asked(void)
{
    uint8_t in[4] = { 9, 8, 7, 6 };
    uint8_t out[5];

    tfw_boot_handoff_write(in, 3u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, retain.custom, 3u);
    TEST_ASSERT_EQUAL_HEX8(0x00, retain.custom[3]);
    memset(out, 0xEE, sizeof out);
    tfw_boot_handoff_read(out, 4u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(retain.custom, out, 4u);
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[4]);
}

static void test_handoff_never_passes_the_retained_area(void)
{
    uint8_t in[TFW_BOOT_HANDOFF_LEN + 8u];
    uint8_t out[TFW_BOOT_HANDOFF_LEN + 8u];

    memset(in, 0xAB, sizeof in);
    tfw_boot_handoff_write(in, (uint8_t)sizeof in);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xAB, retain.custom, TFW_BOOT_HANDOFF_LEN);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, retain.guard, sizeof retain.guard);

    tfw_boot_handoff_write(in, TFW_BOOT_HANDOFF_LEN + 1u);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, retain.guard, sizeof retain.guard);

    memset(retain.guard, 0xCD, sizeof retain.guard);
    memset(out, 0x11, sizeof out);
    tfw_boot_handoff_read(out, TFW_BOOT_HANDOFF_LEN + 1u);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xAB, out, TFW_BOOT_HANDOFF_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x11, out[TFW_BOOT_HANDOFF_LEN]);
}

/* ---- control ------------------------------------------------------------ */

static tfw_reset_cause_t cause_of(soc_reset_reason_t r)
{
    reason = r;
    return tfw_boot_reset_cause();
}

static void test_the_reset_cause_of_each_shared_reason(void)
{
    TEST_ASSERT_EQUAL(TFW_RESET_POWER_ON, cause_of(RESET_REASON_CHIP_POWER_ON));
    TEST_ASSERT_EQUAL_INT(0, reason_cpu);
    TEST_ASSERT_EQUAL(TFW_RESET_BROWNOUT, cause_of(RESET_REASON_SYS_BROWN_OUT));
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, cause_of(RESET_REASON_CORE_SW));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, cause_of(RESET_REASON_CORE_DEEP_SLEEP));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, cause_of((soc_reset_reason_t)0x7F));
}

/* The target's own names: the P4 build answers the P4's, the C3 build the
 * C3's, and each reads the other family as unknown. */
static void test_the_reset_cause_of_each_targets_reasons(void)
{
    static const soc_reset_reason_t p4[] = {
        RESET_REASON_CORE_MWDT, RESET_REASON_CORE_RWDT, RESET_REASON_CPU_MWDT,
        RESET_REASON_CPU_RWDT, RESET_REASON_SYS_RWDT,
    };
    static const soc_reset_reason_t c3[] = {
        RESET_REASON_CORE_MWDT0, RESET_REASON_CORE_RTC_WDT, RESET_REASON_CPU0_MWDT0,
        RESET_REASON_CPU0_RTC_WDT, RESET_REASON_SYS_RTC_WDT,
    };
    const soc_reset_reason_t *mine  = CONFIG_IDF_TARGET_ESP32P4 ? p4 : c3;
    const soc_reset_reason_t *other = CONFIG_IDF_TARGET_ESP32P4 ? c3 : p4;
    size_t i;

    for (i = 0; i < 5u; i++) {
        TEST_ASSERT_EQUAL(TFW_RESET_WATCHDOG, cause_of(mine[i]));
        TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, cause_of(other[i]));
    }
    TEST_ASSERT_EQUAL(TFW_RESET_SOFTWARE, cause_of(CONFIG_IDF_TARGET_ESP32P4 ? RESET_REASON_CPU_SW : RESET_REASON_CPU0_SW));
    TEST_ASSERT_EQUAL(TFW_RESET_UNKNOWN, cause_of(CONFIG_IDF_TARGET_ESP32P4 ? RESET_REASON_CPU0_SW : RESET_REASON_CPU_SW));
}

static void test_jump_loads_the_selected_image_then_resets(void)
{
    selected = 2;
    tfw_boot_jump_application();
    TEST_ASSERT_EQUAL_INT(1, loads);
    TEST_ASSERT_EQUAL_INT(2, loaded_index);
    TEST_ASSERT_EQUAL_INT(1, resets);
    TEST_ASSERT_TRUE(load_call < reset_call);
}

static void test_jump_loads_slot_zero(void)
{
    selected = 0;
    tfw_boot_jump_application();
    TEST_ASSERT_EQUAL_INT(0, loaded_index);
    TEST_ASSERT_EQUAL_INT(1, resets);
}

static void test_jump_with_no_selectable_image_only_resets(void)
{
    selected = INVALID_INDEX;
    tfw_boot_jump_application();
    TEST_ASSERT_EQUAL_INT(0, loads);
    TEST_ASSERT_EQUAL_INT(1, resets);
}

static void test_jump_with_no_partition_table_only_resets(void)
{
    table_loads = false;
    tfw_boot_jump_application();
    TEST_ASSERT_EQUAL_INT(0, loads);
    TEST_ASSERT_EQUAL_INT(1, resets);
}

static void test_reset_resets(void)
{
    tfw_boot_reset();
    TEST_ASSERT_EQUAL_INT(1, resets);
    TEST_ASSERT_EQUAL_INT(0, loads);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flash_read_returns_the_bytes_at_any_offset_and_length);
    RUN_TEST(test_flash_read_of_255_unaligned_bytes);
    RUN_TEST(test_a_failed_flash_read_reads_as_erased);
    RUN_TEST(test_a_flash_read_failing_on_a_later_piece_reads_as_erased);
    RUN_TEST(test_flash_read_of_nothing_reads_nothing);
    RUN_TEST(test_the_page_is_the_4k_sector);
    RUN_TEST(test_a_page_write_erases_then_writes_its_sector);
    RUN_TEST(test_a_page_write_encrypts_when_flash_encryption_is_on);
    RUN_TEST(test_a_page_write_off_a_sector_boundary_is_refused);
    RUN_TEST(test_a_failed_erase_writes_nothing);
    RUN_TEST(test_a_failed_write_is_internal);
    RUN_TEST(test_nvm_read_reads_the_tfw_plat_partition);
    RUN_TEST(test_nvm_read_without_the_partition_is_unsupported);
    RUN_TEST(test_the_first_entry_with_the_label_is_used);
    RUN_TEST(test_the_table_ends_at_the_first_entry_without_magic);
    RUN_TEST(test_the_last_entry_of_a_full_table_is_found);
    RUN_TEST(test_nothing_past_the_table_is_read_as_an_entry);
    RUN_TEST(test_an_unmappable_table_is_unsupported);
    RUN_TEST(test_nvm_read_stays_inside_the_partition);
    RUN_TEST(test_a_failed_nvm_read_is_internal);
    RUN_TEST(test_nvm_write_rewrites_its_bytes_and_keeps_the_rest);
    RUN_TEST(test_nvm_write_to_the_sectors_last_byte);
    RUN_TEST(test_nvm_write_stays_inside_a_small_partition);
    RUN_TEST(test_nvm_write_without_the_partition_is_unsupported);
    RUN_TEST(test_a_failed_sector_read_writes_nothing);
    RUN_TEST(test_nvm_write_passes_on_the_page_writes_status);
    RUN_TEST(test_auth_read_reads_the_record_as_written);
    RUN_TEST(test_auth_read_without_the_partition_needs_authorization);
    RUN_TEST(test_auth_read_of_a_partition_smaller_than_a_record);
    RUN_TEST(test_a_failed_auth_read_is_internal);
    RUN_TEST(test_the_gate_reads_its_trailer_through_this_hal);
    RUN_TEST(test_an_erased_auth_record_authorizes_nothing);
    RUN_TEST(test_uart_rx_reports_a_waiting_byte);
    RUN_TEST(test_uart_tx_sends_the_byte);
    RUN_TEST(test_handoff_round_trips_through_the_retained_record);
    RUN_TEST(test_handoff_copies_no_more_than_asked);
    RUN_TEST(test_handoff_never_passes_the_retained_area);
    RUN_TEST(test_the_reset_cause_of_each_shared_reason);
    RUN_TEST(test_the_reset_cause_of_each_targets_reasons);
    RUN_TEST(test_jump_loads_the_selected_image_then_resets);
    RUN_TEST(test_jump_loads_slot_zero);
    RUN_TEST(test_jump_with_no_selectable_image_only_resets);
    RUN_TEST(test_jump_with_no_partition_table_only_resets);
    RUN_TEST(test_reset_resets);
    return UNITY_END();
}
