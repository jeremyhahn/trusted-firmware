/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A flat register model of the RP2350 for host tests: every address the
 * code under test touches gets one 32-bit cell, zero until written.
 *
 * Each address is its own cell, the atomic set and clear aliases included,
 * so a test sees a write through TFW_RP_SET or TFW_RP_CLR at that alias's
 * address. A test that needs a polled status bit (RESET_DONE) pokes it
 * before the call.
 */
#ifndef TFW_RP2350_MODEL_H
#define TFW_RP2350_MODEL_H

#include <stdint.h>

volatile uint32_t *tfw_rp_model_reg(uintptr_t addr);

/* Forget every cell. */
void     tfw_rp_model_reset(void);
uint32_t tfw_rp_model_peek(uintptr_t addr);
void     tfw_rp_model_poke(uintptr_t addr, uint32_t value);

#endif /* TFW_RP2350_MODEL_H */
