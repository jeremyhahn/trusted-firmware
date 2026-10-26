/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's STM32H5 flash driver (#14): the program and erase sequence
 * behind boot_soc.h's tfw_cm_boot_flash_write. Boot-only: the application has
 * no flash access at all (boot/boot_hal.h, item 2).
 *
 * The driver reaches the part through a context, the flash interface's
 * registers and the flash itself, so that the host test hands it a register
 * block and a flash of its own (test/unit/boot/test_boot_stm32h5_flash.c).
 * The one thing it does not do itself is wait for an operation to finish:
 * tfw_h5_flash_wait is boot_flash.c's on the part and the test's on the host,
 * where the fake hardware acts.
 *
 * The flash is programmed a quad-word (128 bits, 16 bytes) at a time, and a
 * quad-word is programmed once between erases: its ECC is written with it. It
 * is erased a sector (8 KiB) at a time. So an update is one of three things:
 *
 *   - every byte already holds the data: nothing is written;
 *   - every quad-word the data touches is erased (all 0xFF): those quad-words
 *     are programmed, and nothing is erased;
 *   - otherwise each sector the data touches is copied to RAM, the data laid
 *     over the copy, the sector erased and the copy programmed back.
 *
 * The last is a window in which a reset loses the sector's other bytes; the
 * board page (docs/boards/stm32h573i-dk.md) carries it as an owner question.
 */
#ifndef TFW_BOOT_H5_FLASH_H
#define TFW_BOOT_H5_FLASH_H

#include <stdint.h>

#include "tfw/status.h"

struct tfw_h5_flash {
    volatile uint32_t *regs;   /* the FLASH interface, TFW_H5_FLASH_BASE */
    uint8_t           *mem;    /* the flash's first byte, TFW_H5_FLASH_MEM */
};

/* Writes `len` bytes at `off` from the flash's first byte, as above, and
 * reads them back. TFW_ERR_RANGE past the end of bank 2;
 * TFW_ERR_PROTECTED_REGION when the flash stays locked or reports a write
 * protection error; TFW_ERR_INTERNAL for any other error the interface
 * reports, or data that does not read back. An update that writes leaves the
 * interface locked, failed or not. `data` is in RAM: a rewrite erases the
 * sector it is copied from. */
tfw_status_t tfw_h5_flash_update(const struct tfw_h5_flash *f, uint32_t off,
                                 const uint8_t *data, uint32_t len);

/* Returns when the interface has no operation in flight: BSY, WBNE and DBNE
 * all clear in NSSR. */
void tfw_h5_flash_wait(const struct tfw_h5_flash *f);

#endif /* TFW_BOOT_H5_FLASH_H */
