/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A sparse register file for the MCXN94x HAL's host tests: every register the
 * code under test touches gets a word on first access, and every access is
 * logged in order with the value a watched register held at that moment (the
 * value before the access's own store lands). cm_regs.h in this directory
 * sends TFW_CM_REG here.
 */
#ifndef TFW_MCXN_MODEL_H
#define TFW_MCXN_MODEL_H

#include <stddef.h>
#include <stdint.h>

volatile uint32_t *tfw_mcxn_model_reg(uintptr_t addr);

/* Every register back to 0, the log empty, nothing watched. */
void      tfw_mcxn_model_reset(void);
/* The register's value, 0 if never touched; neither call is logged. */
uint32_t  tfw_mcxn_model_get(uintptr_t addr);
void      tfw_mcxn_model_set(uintptr_t addr, uint32_t v);
/* The register whose value each logged access records. */
void      tfw_mcxn_model_watch(uintptr_t addr);

size_t    tfw_mcxn_model_log_len(void);
uintptr_t tfw_mcxn_model_log_addr(size_t i);
uint32_t  tfw_mcxn_model_log_watched(size_t i);
/* The index of the first logged access to addr, or the log's length. */
size_t    tfw_mcxn_model_log_first(uintptr_t addr);

#endif /* TFW_MCXN_MODEL_H */
