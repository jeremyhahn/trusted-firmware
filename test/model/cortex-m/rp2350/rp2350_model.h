/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register model of the RP2350 blocks the tone HAL drives: RESETS, TICKS,
 * TIMER1, IO_BANK0, the pads, SIO and the NVIC enable registers.
 *
 * Every access hands out a slot holding the register's read value; the next
 * access (or any model call) compares the slot with what it handed out and,
 * if the code under test stored something else, applies that write: the
 * peripheral SET/CLR/XOR aliases, SIO's set, clear and XOR registers,
 * RESETS_DONE following RESET, TIMER1's ARMED and INTR write-1 registers and
 * ALARM0 arming. Each write is logged in order, so a test can check the
 * order a sequence ran in.
 */
#ifndef TFW_RP2350_MODEL_H
#define TFW_RP2350_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define RP_MODEL_RESETS_RESET   0x40020000u
#define RP_MODEL_RESETS_DONE    0x40020008u
#define RP_MODEL_TICKS_T1_CTRL  0x40108024u
#define RP_MODEL_TICKS_T1_CYC   0x40108028u
#define RP_MODEL_T1_TIMELR      0x400B800Cu
#define RP_MODEL_T1_ALARM0      0x400B8010u
#define RP_MODEL_T1_ARMED       0x400B8020u
#define RP_MODEL_T1_INTR        0x400B803Cu
#define RP_MODEL_T1_INTE        0x400B8040u
#define RP_MODEL_SIO_OUT        0xD0000010u
#define RP_MODEL_SIO_OUT_SET    0xD0000018u
#define RP_MODEL_SIO_OUT_CLR    0xD0000020u
#define RP_MODEL_SIO_OUT_XOR    0xD0000028u
#define RP_MODEL_SIO_OE         0xD0000030u
#define RP_MODEL_SIO_OE_SET     0xD0000038u
#define RP_MODEL_SIO_OE_CLR     0xD0000040u
#define RP_MODEL_IO_CTRL(n)     (0x40028004u + 8u * (n))
#define RP_MODEL_PADS(n)        (0x40038004u + 4u * (n))
#define RP_MODEL_NVIC_ISER0     0xE000E100u
#define RP_MODEL_RESET_TIMER1   (1u << 24)

/* The register model's slot for addr (cm_regs.h's TFW_CM_REG). */
volatile uint32_t *tfw_rp_model_reg(uintptr_t addr);

/* Power-on: every peripheral in reset, every register 0, the clock at 0. */
void tfw_rp_model_reset(void);

/* TIMELR's value from now on. */
void tfw_rp_model_set_time(uint32_t ticks);

/* A register's state: OUT and OE for SIO, armed and pending bits for
 * TIMER1's ARMED and INTR, the stored value otherwise. */
uint32_t tfw_rp_model_read(uintptr_t addr);

/* The index in the write log of the first write of value to addr at or
 * after index from, or -1. tfw_rp_model_writes counts the log. */
int tfw_rp_model_find(uintptr_t addr, uint32_t value, int from);
int tfw_rp_model_writes(void);

/* Fire ALARM0 as TIMELR reaching it would, if armed with INTE bit 0 set:
 * the alarm disarms, INTR bit 0 rises and tfw_cm_irq4 runs. Returns 1 if
 * it fired. */
int tfw_rp_model_fire(void);

#endif /* TFW_RP2350_MODEL_H */
