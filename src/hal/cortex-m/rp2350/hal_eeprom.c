/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR's EEPROM on the RP2350 (row rp2350), where the platform window is
 * QSPI flash behind XIP and a store to it writes nothing. The AVR's map is
 * kept: an address below TFW_BOOT_EE_LEN is tfw-boot's span, read from the
 * platform window (TFW_CM_PLATFORM_BASE) as the generic file reads it, and a
 * write into it is refused as the generic file refuses it, the confirm
 * request (TFW_BOOT_CONFIRM_ADDR, TFW_BOOT_CONFIRM_LEN) excepted, which is a
 * read-modify-write of the platform block's first sector as below, so that
 * tfw-boot's tfw_boot_nvm_read finds it (a store through XIP writes
 * nothing). From TFW_BOOT_EE_LEN up to TFW_EEPROM_SIZE an
 * address is a byte of the application's own sector, TFW_CM_USER_EE_BASE
 * (board_profile.h), the sector directly below the platform block's.
 *
 * A write is a read-modify-write of that one sector, through tfw-boot's flash
 * helper (boot/hal/cortex-m/rp2350/rp_flash.h): tfw_rp_flash_update reads the
 * sector into its RAM buffer, merges the bytes, and erases and programs it
 * whole through the boot ROM with XIP off (rp_flash_rom.c), then the bytes are
 * read back through XIP and compared; a mismatch is TFW_ERR_INTERNAL, the
 * status the ESP32's flash EEPROM gives a failed flash write (status.h has
 * no I/O code). The span is
 * checked before anything is touched, so a refused write writes nothing, and
 * no user address this file computes reaches the platform sector: the
 * _Static_asserts below hold the user span inside the user sector, and the
 * confirm request inside the platform block's first sector. A power
 * loss between the erase and the program loses the user sector only, as a
 * power loss mid-write loses the AVR's EEPROM bytes; during the confirm
 * request's write it loses the platform sector, as during tfw-boot's own
 * write of the request's clear.
 */
#include <stdint.h>
#include <string.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/boot.h"
#include "rp_flash.h"
#include "soc_regs.h"

void tfw_cm_tick_credit(tfw_ms_t ms);   /* hal_tick.c */

#define USER_EE_LEN ((uint32_t)TFW_EEPROM_SIZE - (uint32_t)TFW_BOOT_EE_LEN)

_Static_assert(TFW_BOOT_EE_BASE == 0u, "the span starts at the EEPROM's zero");
_Static_assert((uint32_t)TFW_EEPROM_SIZE > (uint32_t)TFW_BOOT_EE_LEN,
               "the EEPROM has bytes past tfw-boot's span");
_Static_assert(USER_EE_LEN <= (uint32_t)TFW_CM_USER_EE_SIZE
               && (uint32_t)TFW_CM_USER_EE_SIZE == TFW_RP_FLASH_SECTOR,
               "the user span fits the one user sector");
_Static_assert((uint32_t)TFW_CM_USER_EE_BASE % TFW_RP_FLASH_SECTOR == 0u
               && (uint32_t)TFW_CM_USER_EE_BASE + TFW_CM_USER_EE_SIZE
                  <= (uint32_t)TFW_CM_PLATFORM_BASE,
               "the user sector is a whole sector below the platform block");
_Static_assert((uint32_t)TFW_CM_PLATFORM_BASE % TFW_RP_FLASH_SECTOR == 0u
               && (uint32_t)TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN
                  <= TFW_RP_FLASH_SECTOR,
               "the confirm request is inside the platform block's first sector");

#define USER_EE ((const uint8_t *)(uintptr_t)TFW_CM_USER_EE_BASE)

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    uint32_t end = (uint32_t)addr + len;
    uint32_t low = 0u;

    if (end > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if (addr < TFW_BOOT_EE_LEN) {
        tfw_status_t st;

        low = (end < TFW_BOOT_EE_LEN ? end : TFW_BOOT_EE_LEN) - addr;
        st  = tfw_hal_nvm_read(addr, buf, (uint8_t)low);
        if (st != TFW_OK) {
            return st;
        }
    }
    if (end > (uint32_t)addr + low) {
        memcpy(&buf[low], &USER_EE[(uint32_t)addr + low - TFW_BOOT_EE_LEN], len - low);
    }
    return TFW_OK;
}

/* The sector erase and program run with interrupts masked (rp_flash_rom.c),
 * tens of milliseconds, and SysTick's expirations in that time collapse into
 * the one interrupt pending at the unmask. The write runs masked from here and
 * credits hal_tick.c's clock with the expirations it hid: the SysTick reloads
 * crossed are the cycles from the last reload before entry to the last reload
 * before exit, the masked time on TIMER0 plus SysTick's count into its period
 * at entry less that at exit, so each write counts the boundaries it crossed
 * whatever its phase. One of them is the pending interrupt, unless one was
 * pending already at entry, which then stands for an earlier boundary.
 * [ARM-v8M] B11.2 (SysTick), D1.2.135 (ICSR.PENDSTSET). */
#define EE_ICSR            TFW_CM_REG(0xE000ED04u)
#define EE_ICSR_PENDSTSET  (1uL << 26)
#define EE_CYCLES_PER_US   ((uint32_t)(TFW_CM_CPU_HZ / 1000000uL))

_Static_assert(TFW_CM_CPU_HZ % 1000000uL == 0u, "SysTick counts whole cycles per microsecond");

typedef struct {
    uint32_t t0;        /* TIMER0 at entry, microseconds */
    uint32_t into;      /* SysTick cycles into its period at entry */
    uint32_t pending;   /* SysTick pending at entry */
} masked_entry_t;

static void masked_enter(masked_entry_t *e)
{
    uint32_t cvr;
    uint32_t again;

    /* The phase and the pending bit must agree: an expiration between the
     * two reads would be both one of the periods crossed and the pending
     * tick the handler counts. Read the phase on both sides of the pending
     * bit and read again when SysTick reloaded (or reached zero) between.
     * A stopped SysTick is never credited, so one read does then. */
    do {
        e->t0      = TFW_RP_TIMER0_TIMERAWL;
        cvr        = TFW_CM_SYST_CVR;
        e->pending = EE_ICSR & EE_ICSR_PENDSTSET;
        again      = TFW_CM_SYST_CVR;
    } while ((TFW_CM_SYST_CSR & TFW_CM_SYST_CSR_ENABLE) != 0u &&
             (again > cvr || again == 0u));
    e->into = TFW_CM_SYST_RVR - cvr;
}

static void credit_masked_ms(const masked_entry_t *e)
{
    uint32_t period = TFW_CM_SYST_RVR + 1u;
    uint32_t cycles = (TFW_RP_TIMER0_TIMERAWL - e->t0) * EE_CYCLES_PER_US;
    uint32_t out    = TFW_CM_SYST_RVR - TFW_CM_SYST_CVR;
    uint32_t crossed;

    if ((TFW_CM_SYST_CSR & TFW_CM_SYST_CSR_ENABLE) == 0u) {
        return;
    }
    /* TIMER0 counts whole microseconds: round to the nearest period. */
    crossed = (e->into + cycles + period / 2u - out) / period;
    if (crossed > 0u && e->pending == 0u) {
        crossed--;
    }
    if (crossed > 0u) {
        tfw_cm_tick_credit((tfw_ms_t)crossed);
    }
}

/* CRC-32 (reflected, 0xEDB88320) of the user sector as it reads, with
 * [at, at + len) taken from buf instead. A write erases and reprograms all
 * of the sector and the ROM's program call reports no error, so the sector
 * the write expects is taken before it and the whole sector checked after:
 * a fault in a byte the write keeps fails the write too, with no RAM copy. */
static uint32_t sector_crc(const uint8_t *sec, uint32_t at, const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    unsigned bit;

    for (i = 0u; i < (uint32_t)TFW_RP_FLASH_SECTOR; i++) {
        uint8_t b = (i >= at && i - at < len) ? buf[i - at] : sec[i];

        crc ^= b;
        for (bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* len bytes at byte at of the sector at base (an XIP address), through the
 * flash helper with interrupts masked, then read back through XIP: the
 * bytes written and the sector's CRC with them merged in, so a kept byte the
 * rewrite corrupted is caught as well as a written one. */
static tfw_status_t sector_write(uint32_t base, uint32_t at, const uint8_t *buf, uint8_t len)
{
    const uint8_t *sec = (const uint8_t *)(uintptr_t)base;
    uint32_t primask;
    masked_entry_t entry;
    tfw_status_t st;
    uint32_t want;

    want    = sector_crc(sec, at, buf, len);
    primask = tfw_cm_irq_save();
    masked_enter(&entry);
    st      = tfw_rp_flash_update(base + at, buf, len, (uint32_t)TFW_FLASH_TOTAL);
    credit_masked_ms(&entry);
    tfw_cm_irq_restore(primask);
    if (st != TFW_OK) {
        return st;
    }
    if (memcmp(&sec[at], buf, len) != 0 || sector_crc(sec, 0u, buf, 0u) != want) {
        return TFW_ERR_INTERNAL;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    if (addr < TFW_BOOT_EE_LEN) {
        if (addr != TFW_BOOT_CONFIRM_ADDR || len != TFW_BOOT_CONFIRM_LEN) {
            return TFW_ERR_RANGE;
        }
        /* The confirm request into the platform sector, where tfw-boot's
         * tfw_boot_nvm_read finds it: a store through XIP writes nothing. */
        return sector_write((uint32_t)TFW_CM_PLATFORM_BASE, (uint32_t)addr, buf, len);
    }
    if (len == 0u) {
        return TFW_OK;
    }
    return sector_write((uint32_t)TFW_CM_USER_EE_BASE, (uint32_t)addr - TFW_BOOT_EE_LEN, buf,
                        len);
}
