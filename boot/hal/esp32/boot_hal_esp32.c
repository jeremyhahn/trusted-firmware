/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/boot_hal.h on ESP-IDF's second-stage bootloader.
 *
 * Linked into the bootloader beside layer 0 (boot_l0.c), which uses the
 * handoff and the reset cause; --gc-sections drops what nothing calls. The
 * rest is here so the contract is whole on this architecture, and every
 * entry is ESP-IDF's bootloader support or ROM, never a driver of ours:
 *
 *   flash     bootloader_flash_*, which encrypt and decrypt when flash
 *             encryption is on. A "page" is the 4 KB flash sector, the unit
 *             the part erases (#11: the contract's 16-bit page size and
 *             256-byte OTA page are the AVR's; this answers 4096).
 *   nvm       the `tfw_plat` data partition, found in the partition table
 *             by label -- the same block the application reads through
 *             tfw_hal_nvm_read; and `tfw_auth`, the activate authorization
 *             the application writes (tfw_l0_auth_read)
 *   uart      the ROM console, polled
 *   handoff   the custom area of ESP-IDF's RTC-retained record
 *   control   ESP-IDF's loader and ESP-IDF's reset
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_flash_encrypt.h"
#include "esp_flash_partitions.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"
#include "esp_image_format.h"
#include "bootloader_common.h"
#include "bootloader_flash_priv.h"
#include "bootloader_utility.h"
#include "soc/reset_reasons.h"

#include "boot_hal.h"
#include "flash_span.h"
#include "l0_gate.h"
#include "tfw/boot.h"

#define FLASH_PAGE 4096u
#define PLAT_LABEL "tfw_plat"
#define AUTH_LABEL "tfw_auth"
#define BOUNCE     256u                 /* bytes; a multiple of 4 (flash_span.h) */

/* Every read in this file. bootloader_flash_read refuses a source, length or
 * destination that is not 4-byte aligned, and the contract reads at any byte:
 * the SUIT envelope's 2-byte length after the trailer is one (l0_gate.c). So
 * each piece goes through an aligned bounce buffer (flash_span.h). */
static esp_err_t flash_read(uint32_t addr, uint8_t *buf, uint32_t len, bool decrypt)
{
    static uint32_t  bounce[BOUNCE / 4u];
    tfw_flash_span_t s;

    while (len > 0u) {
        tfw_flash_span(addr, len, BOUNCE, &s);
        if (bootloader_flash_read(s.from, bounce, s.len, decrypt) != ESP_OK) {
            return ESP_FAIL;
        }
        memcpy(buf, (const uint8_t *)bounce + s.skip, s.take);
        addr += s.take;
        buf  += s.take;
        len  -= s.take;
    }
    return ESP_OK;
}

void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    if (flash_read(addr, buf, len, true) != ESP_OK) {
        memset(buf, 0xFF, len);         /* what erased flash reads as */
    }
}

tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    /* Words: bootloader_flash_write refuses a source not 4-byte aligned. */
    static uint32_t page[FLASH_PAGE / 4u];

    if (addr % FLASH_PAGE != 0u) {
        return TFW_ERR_RANGE;
    }
    memcpy(page, data, FLASH_PAGE);
    if (bootloader_flash_erase_sector(addr / FLASH_PAGE) != ESP_OK ||
        bootloader_flash_write(addr, page, FLASH_PAGE, esp_flash_encryption_enabled()) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

uint16_t tfw_boot_flash_page_size(void)
{
    return (uint16_t)FLASH_PAGE;
}

/* A data partition's offset and size, by label, from the partition table. */
static int find_partition(const char *label, uint32_t *offset, uint32_t *size)
{
    const esp_partition_info_t *t;
    int                         i;
    int                         found = 0;

    t = bootloader_mmap(ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < (int)(ESP_PARTITION_TABLE_MAX_LEN / sizeof *t); i++) {
        if (t[i].magic != ESP_PARTITION_MAGIC) {
            break;
        }
        if (strncmp((const char *)t[i].label, label, sizeof t[i].label) == 0) {
            *offset = t[i].pos.offset;
            *size   = t[i].pos.size;
            found   = 1;
            break;
        }
    }
    bootloader_munmap(t);
    return found;
}

tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    uint32_t off;
    uint32_t size;

    if (!find_partition(PLAT_LABEL, &off, &size)) {
        return TFW_ERR_UNSUPPORTED;
    }
    if ((uint32_t)addr + len > size) {
        return TFW_ERR_RANGE;
    }
    return flash_read(off + addr, buf, len, true) == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

/* l0_gate.h's: the activate authorization, read as written. The partition
 * is not encrypted -- a signature is not a secret, and the record is its own
 * integrity -- so the read does not decrypt. */
tfw_status_t tfw_l0_auth_read(uint8_t rec[TFW_BOOT_AUTH_LEN])
{
    uint32_t off;
    uint32_t size;

    if (!find_partition(AUTH_LABEL, &off, &size)) {
        return TFW_ERR_AUTH_REQUIRED;
    }
    if (size < TFW_BOOT_AUTH_LEN) {
        return TFW_ERR_RANGE;
    }
    return flash_read(off, rec, TFW_BOOT_AUTH_LEN, false) == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    static uint8_t sector[FLASH_PAGE];
    uint32_t       off;
    uint32_t       size;

    if (!find_partition(PLAT_LABEL, &off, &size)) {
        return TFW_ERR_UNSUPPORTED;
    }
    if ((uint32_t)addr + len > size || (uint32_t)addr + len > FLASH_PAGE) {
        return TFW_ERR_RANGE;
    }
    if (flash_read(off, sector, FLASH_PAGE, true) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    memcpy(&sector[addr], buf, len);
    return tfw_boot_flash_write_page(off, sector);
}

void tfw_boot_uart_init(uint32_t baud)
{
    (void)baud;                         /* the ROM console is already configured */
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    return (uint8_t)(esp_rom_output_rx_one_char(out) == 0);
}

void tfw_boot_uart_tx(uint8_t byte)
{
    (void)esp_rom_output_tx_one_char(byte);
}

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    uint8_t *dst = bootloader_common_get_rtc_retain_mem()->custom;

    memcpy(dst, block, len > TFW_BOOT_HANDOFF_LEN ? TFW_BOOT_HANDOFF_LEN : len);
}

void tfw_boot_handoff_read(uint8_t *buf, uint8_t len)
{
    const uint8_t *src = bootloader_common_get_rtc_retain_mem()->custom;

    memcpy(buf, src, len > TFW_BOOT_HANDOFF_LEN ? TFW_BOOT_HANDOFF_LEN : len);
}

/* The ROM's reset reason, which nothing before the bootloader clears. A
 * software reset is named, but the ROM cannot tell a requested restart from a
 * panic -- both end in a software reset -- so it reports TFW_RESET_SOFTWARE and
 * the application refines it from ESP-IDF's own record (#48). Deep-sleep wakes
 * have no wire value and read as unknown. */
tfw_reset_cause_t tfw_boot_reset_cause(void)
{
    switch (esp_rom_get_reset_reason(0)) {
    case RESET_REASON_CHIP_POWER_ON: return TFW_RESET_POWER_ON;
    case RESET_REASON_SYS_BROWN_OUT: return TFW_RESET_BROWNOUT;
    case RESET_REASON_CORE_SW:       return TFW_RESET_SOFTWARE;
#if CONFIG_IDF_TARGET_ESP32P4
    case RESET_REASON_CPU_SW:        return TFW_RESET_SOFTWARE;
    /* The P4's ROM names its watchdog resets without an instance number. */
    case RESET_REASON_CORE_MWDT:
    case RESET_REASON_CORE_RWDT:
    case RESET_REASON_CPU_MWDT:
    case RESET_REASON_CPU_RWDT:
    case RESET_REASON_SYS_RWDT:
#else
    case RESET_REASON_CPU0_SW:       return TFW_RESET_SOFTWARE;
    case RESET_REASON_CORE_MWDT0:
    case RESET_REASON_CORE_RTC_WDT:
    case RESET_REASON_CPU0_MWDT0:
    case RESET_REASON_CPU0_RTC_WDT:
    case RESET_REASON_SYS_RTC_WDT:
#endif
                                     return TFW_RESET_WATCHDOG;
    default:                         return TFW_RESET_UNKNOWN;
    }
}

/* ESP-IDF's loader: it chooses the slot, verifies it under secure boot and
 * jumps. Layer 0's own path (boot_l0.c) calls the same two functions. */
void tfw_boot_jump_application(void)
{
    bootloader_state_t bs = { 0 };

    if (bootloader_utility_load_partition_table(&bs)) {
        int index = bootloader_utility_get_selected_boot_partition(&bs);

        if (index != INVALID_INDEX) {
            bootloader_utility_load_boot_image(&bs, index);
        }
    }
    bootloader_reset();
}

void tfw_boot_reset(void)
{
    bootloader_reset();
}
