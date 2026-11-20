/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC code's entry points (src/hal/cortex-m/nrf54l/hal_soc.c)
 * behind names of the host's, for test/unit/test_nrf54l_soc.c, as
 * imx95_entry.c does for the i.MX 95: the test outside this directory names
 * nothing of the core, and the arch-leak gate exempts this directory.
 */
#ifndef TFW_NRF54L_SOC_ENTRY_H
#define TFW_NRF54L_SOC_ENTRY_H

#include <stdint.h>

void tfw_nrf54l_model_soc_early(void);
void tfw_nrf54l_model_watchdog_arm(void);
void tfw_nrf54l_model_watchdog_pet(void);

/* hal_early.c's marker word, which the arm writes first. */
uint32_t tfw_nrf54l_model_mark(void);
void     tfw_nrf54l_model_mark_clear(void);

/* A word of the register file set before the call, by address. */
void tfw_nrf54l_model_poke(uintptr_t addr, uint32_t val);

#endif /* TFW_NRF54L_SOC_ENTRY_H */
