/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350's QSPI NOR flash, TIMER0 and SysTick for the flash EEPROM test
 * (nor_model.c). The board's user and platform sectors are mapped at their
 * XIP addresses; the model stands in for rp_flash_rom.c's one-sector read
 * and write under tfw-boot's read-modify-write.
 */
#ifndef TFW_TEST_RP2350_NOR_MODEL_H
#define TFW_TEST_RP2350_NOR_MODEL_H

#include <stdint.h>

#define RP_NOR_MAX_ERASES 16u

/* Each sector write's offset from the XIP base, in order. */
extern uint32_t rp_nor_erases[RP_NOR_MAX_ERASES];
extern unsigned rp_nor_n_erases;
/* Nonzero: the next program flips the sector's last byte. */
extern int      rp_nor_corrupt_next;
/* Microseconds one sector write takes, TIMER0's count and SysTick's phase
 * both advancing by it while interrupts are masked. */
extern uint32_t rp_nor_write_us;

/* Map the user and platform sectors; 0 when the host cannot. */
int  rp_nor_model_map(void);
/* The user sector erased, the platform sector 0xA5, no erases, no write
 * time, interrupts unmasked and none pending. */
void rp_nor_model_reset(void);

/* The model's interrupt mask, which hal_eeprom.c reaches through the host's
 * cm_regs.h; a test that drives the sector write itself masks with these. */
uint32_t tfw_rp_model_irq_save(void);
void     tfw_rp_model_irq_restore(uint32_t primask);

#endif /* TFW_TEST_RP2350_NOR_MODEL_H */
