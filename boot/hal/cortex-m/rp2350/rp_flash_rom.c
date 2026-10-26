/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * One flash sector on the RP2350, through the boot ROM (rp_flash.h).
 *
 * tfw-boot runs from the XIP window over the same flash it writes, and while
 * the ROM erases or programs, XIP is off: no instruction or constant can be
 * fetched from flash. So the sequence is one function in SRAM
 * (rp_flash_run, in .ramfunc, which mk/arch/cortex-m/rp2350.ld copies with
 * .data), it calls nothing but the ROM, through pointers the flash-resident
 * caller looked up beforehand and passed on its stack, and it programs from
 * rp_flash.c's SRAM buffer. Interrupts are masked across it: tfw-boot enables
 * none, and a fault taken with XIP off would find its handler unreadable.
 *
 * The ROM's sequence, as the vendor's SDK runs it: connect the flash's pins,
 * leave XIP, erase, program, flush the XIP cache (which also releases the
 * chip select), and back to XIP with the plain 03h read command. That read
 * is slow; it lasts until the next reset, when the ROM sets the window up
 * again, and tfw-boot jumps or resets after a platform write.
 *
 * The ROM's lookup is rp_rom_lookup.c's, so the host test
 * (boot/hal/cortex-m/rp2350/test/test_boot_rp2350_rom.c) runs this file
 * whole over a ROM table model and a cm_regs.h of its own.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tfw/config.h"
#include "cm_regs.h"
#include "rp_flash.h"

typedef void (*rp_rom_void_fn)(void);
typedef void (*rp_rom_erase_fn)(uint32_t off, size_t count, uint32_t block_size,
                                uint8_t block_cmd);
typedef void (*rp_rom_program_fn)(uint32_t off, const uint8_t *data, size_t count);

struct rp_rom_flash {
    rp_rom_void_fn    connect;
    rp_rom_void_fn    exit_xip;
    rp_rom_erase_fn   erase;
    rp_rom_program_fn program;
    rp_rom_void_fn    flush;
    rp_rom_void_fn    enter_xip;
};

/* The flash's size: the offsets the ROM takes run from 0 to here. */
#define RP_FLASH_SIZE ((uint32_t)(TFW_FLASH_TOTAL - TFW_RP_XIP_BASE))

/* In SRAM, called from flash (cm_regs.h, TFW_CM_RAMFUNC). */
TFW_CM_RAMFUNC("rp_flash_run")
static void rp_flash_run(const struct rp_rom_flash *rom, uint32_t off, const uint8_t *buf)
{
    rom->connect();
    rom->exit_xip();
    rom->erase(off, TFW_RP_FLASH_SECTOR, TFW_RP_FLASH_SECTOR,
               (uint8_t)TFW_RP_FLASH_SECTOR_ERASE_CMD);
    rom->program(off, buf, TFW_RP_FLASH_SECTOR);
    rom->flush();
    rom->enter_xip();
}

void tfw_rp_flash_sector_read(uint32_t off, uint8_t *buf)
{
    memcpy(buf, (const void *)(uintptr_t)(TFW_RP_XIP_BASE + off), TFW_RP_FLASH_SECTOR);
}

tfw_status_t tfw_rp_flash_sector_write(uint32_t off, const uint8_t *buf)
{
    struct rp_rom_flash rom;
    uint32_t primask;

    /* A whole sector of the flash, or nothing: the ROM checks neither. */
    if ((off % TFW_RP_FLASH_SECTOR) != 0u || off > RP_FLASH_SIZE - TFW_RP_FLASH_SECTOR) {
        return TFW_ERR_RANGE;
    }
    rom.connect   = (rp_rom_void_fn)tfw_rp_rom_func(TFW_RP_ROM_CONNECT_INTERNAL_FLASH);
    rom.exit_xip  = (rp_rom_void_fn)tfw_rp_rom_func(TFW_RP_ROM_FLASH_EXIT_XIP);
    rom.erase     = (rp_rom_erase_fn)tfw_rp_rom_func(TFW_RP_ROM_FLASH_RANGE_ERASE);
    rom.program   = (rp_rom_program_fn)tfw_rp_rom_func(TFW_RP_ROM_FLASH_RANGE_PROGRAM);
    rom.flush     = (rp_rom_void_fn)tfw_rp_rom_func(TFW_RP_ROM_FLASH_FLUSH_CACHE);
    rom.enter_xip = (rp_rom_void_fn)tfw_rp_rom_func(TFW_RP_ROM_FLASH_ENTER_CMD_XIP);
    if (rom.connect == NULL || rom.exit_xip == NULL || rom.erase == NULL ||
        rom.program == NULL || rom.flush == NULL || rom.enter_xip == NULL) {
        return TFW_ERR_UNSUPPORTED;     /* not this ROM: nothing was touched */
    }

    primask = tfw_cm_irq_save();
    rp_flash_run(&rom, off, buf);
    tfw_cm_dsb();
    tfw_cm_isb();
    tfw_cm_irq_restore(primask);
    return TFW_OK;
}
