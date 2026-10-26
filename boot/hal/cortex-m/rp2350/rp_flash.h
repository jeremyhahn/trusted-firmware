/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's flash writes on the RP2350 (row rp2350): the QSPI flash behind
 * the XIP window, written through the boot ROM's flash functions ([RP-DS],
 * boot ROM chapter, "Bootrom APIs": rom_table_lookup and the flash
 * functions; docs/reference/README.md has its title and URL). Checked
 * against [RP-DS] release 8 (build 80d627281ed5), section by section below;
 * the one value that release does not give is cited from pico-sdk.
 *
 *   rp_flash.c      the read-modify-write over 4 KiB sectors, which the host
 *                   test drives (test/unit/boot/test_boot_rp2350_flash.c)
 *   rp_flash_rom.c  one sector's read through XIP and its erase and program
 *                   through the ROM, the sequence run from SRAM, which the
 *                   host test drives over a ROM table model
 *                   (boot/hal/cortex-m/rp2350/test/test_boot_rp2350_rom.c)
 *   rp_rom_lookup.c the ROM's rom_table_lookup, by the pointer at 0x16
 *   boot_flash.c    boot_soc.h's entry, with the board's flash size
 */
#ifndef TFW_BOOT_RP2350_FLASH_H
#define TFW_BOOT_RP2350_FLASH_H

#include <stdint.h>

#include "tfw/status.h"

/* The flash at the base of the XIP window; the ROM's functions take offsets
 * from here. The sector is the erase unit the ROM's range erase falls back to
 * (command 0x20), the page the unit its range program writes. XIP base §2.2
 * table 10; sector, page and 20h §5.4.8.9 and §5.4.8.10-11. */
#define TFW_RP_XIP_BASE        0x10000000u
#define TFW_RP_FLASH_SECTOR    4096u
#define TFW_RP_FLASH_PAGE      256u
#define TFW_RP_FLASH_SECTOR_ERASE_CMD 0x20u

/* The ROM's lookup: a 16-bit pointer to rom_table_lookup at this address,
 * called with a two-character code and the Secure Arm function flag. The
 * pointer's address §5.4.1 table 464; the code, c1 | c2 << 8, §5.4.1; the
 * codes and their signatures §5.4.8.3, .6-.8, .10 and .11. The flag's value
 * is not in [RP-DS] (§5.4.1 names RT_FLAG_FUNC_ARM_SEC but gives no number);
 * it is pico-sdk 2.1.1's (bddd20f928ce),
 * src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h
 * line 62, RT_FLAG_FUNC_ARM_SEC 0x0004. */
#define TFW_RP_ROM_LOOKUP_PTR  0x00000016u
#define TFW_RP_RT_FLAG_FUNC_ARM_SEC 0x0004u
#define TFW_RP_ROM_CODE(c1, c2) ((uint32_t)(c1) | ((uint32_t)(c2) << 8))

#define TFW_RP_ROM_CONNECT_INTERNAL_FLASH TFW_RP_ROM_CODE('I', 'F')
#define TFW_RP_ROM_FLASH_EXIT_XIP         TFW_RP_ROM_CODE('E', 'X')
#define TFW_RP_ROM_FLASH_RANGE_ERASE      TFW_RP_ROM_CODE('R', 'E')
#define TFW_RP_ROM_FLASH_RANGE_PROGRAM    TFW_RP_ROM_CODE('R', 'P')
#define TFW_RP_ROM_FLASH_FLUSH_CACHE      TFW_RP_ROM_CODE('F', 'C')
#define TFW_RP_ROM_FLASH_ENTER_CMD_XIP    TFW_RP_ROM_CODE('C', 'X')

_Static_assert(TFW_RP_FLASH_SECTOR % TFW_RP_FLASH_PAGE == 0u,
               "a sector is whole pages: the ROM programs a sector in one call");

/* len bytes at addr, an address in the XIP window below flash_end (the
 * board's TFW_FLASH_TOTAL), written so every other byte of each sector they
 * touch keeps its value. A sector whose bytes already hold the data is not
 * erased. TFW_ERR_RANGE for a span outside [TFW_RP_XIP_BASE, flash_end);
 * otherwise the first failing sector's status. The span's range against the
 * board's regions is the caller's (boot_hal_cortex-m.c). */
tfw_status_t tfw_rp_flash_update(uint32_t addr, const uint8_t *data, uint32_t len,
                                 uint32_t flash_end);

/* The boot ROM's function for code (TFW_RP_ROM_*), or 0 when this ROM has
 * none: rom_table_lookup with TFW_RP_RT_FLAG_FUNC_ARM_SEC. rp_rom_lookup.c on
 * the part; the ROM test's table model on the host. */
uintptr_t tfw_rp_rom_func(uint32_t code);

/* One sector, by its offset from TFW_RP_XIP_BASE: read it whole, and erase
 * and program it whole. The write refuses, before it touches anything, an
 * offset that is not a sector's or a sector past the board's flash
 * (TFW_ERR_RANGE) and a ROM without one of the six functions
 * (TFW_ERR_UNSUPPORTED). rp_flash_rom.c on the part; the read-modify-write
 * test's own over a RAM flash. */
void         tfw_rp_flash_sector_read(uint32_t off, uint8_t *buf);
tfw_status_t tfw_rp_flash_sector_write(uint32_t off, const uint8_t *buf);

#endif /* TFW_BOOT_RP2350_FLASH_H */
