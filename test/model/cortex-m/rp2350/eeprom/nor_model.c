/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The NOR flash, TIMER0 and SysTick model behind test/unit/
 * test_rp2350_eeprom.c (nor_model.h). A sector write is the part's: it
 * erases the sector to 0xFF and then programs it, and programming only
 * clears bits (new = old & data). It must run with interrupts masked, as
 * rp_flash_rom.c runs it, and it takes rp_nor_write_us: TIMER0 counts on,
 * and SysTick's expirations in that time leave one interrupt pending,
 * taken at the unmask as the part takes it.
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "unity.h"
#include "tfw/config.h"
#include "rp_flash.h"
#include "soc_regs.h"
#include "nor_model.h"

/* TIMERAWL's address, §12.8.5 table 1349, as soc_regs.h has it. */
#define TIMERAWL_ADDR ((uintptr_t)TFW_RP_TIMER0_BASE + 0x28u)
#define WINDOW_BASE   ((uint32_t)TFW_CM_USER_EE_BASE)
#define WINDOW_LEN    ((uint32_t)TFW_FLASH_TOTAL - WINDOW_BASE)
#define FLASH         ((uint8_t *)(uintptr_t)TFW_RP_XIP_BASE)
#define TICK_US       1000u

void SysTick_Handler(void);

uint32_t rp_nor_erases[RP_NOR_MAX_ERASES];
unsigned rp_nor_n_erases;
int      rp_nor_corrupt_next;
uint32_t rp_nor_write_us;

static volatile uint32_t timerawl;
static uint32_t          systick_phase_us;
static int               systick_pending;
static uint32_t          masked;

volatile uint32_t *tfw_rp_model_reg(uintptr_t addr)
{
    if (addr != TIMERAWL_ADDR) {
        fprintf(stderr, "nor_model: no register at 0x%08lx\n", (unsigned long)addr);
        abort();
    }
    return &timerawl;
}

uint32_t tfw_rp_model_irq_save(void)
{
    uint32_t was = masked;

    masked = 1u;
    return was;
}

void tfw_rp_model_irq_restore(uint32_t primask)
{
    masked = primask;
    if (masked == 0u && systick_pending) {
        systick_pending = 0;
        SysTick_Handler();
    }
}

void tfw_rp_flash_sector_read(uint32_t off, uint8_t *buf)
{
    memcpy(buf, &FLASH[off], TFW_RP_FLASH_SECTOR);
}

tfw_status_t tfw_rp_flash_sector_write(uint32_t off, const uint8_t *buf)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, masked, "the sector write runs masked");
    TEST_ASSERT_EQUAL_UINT32(0u, off % TFW_RP_FLASH_SECTOR);
    TEST_ASSERT_TRUE(rp_nor_n_erases < RP_NOR_MAX_ERASES);
    rp_nor_erases[rp_nor_n_erases++] = off;
    memset(&FLASH[off], 0xFF, TFW_RP_FLASH_SECTOR);
    for (i = 0u; i < TFW_RP_FLASH_SECTOR; i++) {
        FLASH[off + i] &= buf[i];
    }
    if (rp_nor_corrupt_next) {
        rp_nor_corrupt_next = 0;
        FLASH[off + TFW_RP_FLASH_SECTOR - 1u] ^= 0x01u;
    }
    timerawl += rp_nor_write_us;
    systick_phase_us += rp_nor_write_us;
    if (systick_phase_us >= TICK_US) {
        systick_pending = 1;
        systick_phase_us %= TICK_US;
    }
    return TFW_OK;
}

int rp_nor_model_map(void)
{
    void *w = mmap((void *)(uintptr_t)WINDOW_BASE, WINDOW_LEN, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (w != (void *)(uintptr_t)WINDOW_BASE) {
        fprintf(stderr, "nor_model: cannot map the flash at 0x%08lx\n",
                (unsigned long)WINDOW_BASE);
        return 0;
    }
    return 1;
}

void rp_nor_model_reset(void)
{
    memset(&FLASH[WINDOW_BASE - TFW_RP_XIP_BASE], 0xFF, TFW_RP_FLASH_SECTOR);
    memset(&FLASH[(uint32_t)TFW_CM_PLATFORM_BASE - TFW_RP_XIP_BASE], 0xA5,
           TFW_RP_FLASH_SECTOR);
    rp_nor_n_erases     = 0u;
    rp_nor_corrupt_next = 0;
    rp_nor_write_us     = 0u;
    systick_pending     = 0;
    masked              = 0u;
}
