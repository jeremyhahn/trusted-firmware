/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC code's entry points behind the host's names
 * (nrf54l_soc_entry.h), on the register file (regfile_model.c).
 */
#include <stdint.h>

#include "cm_regs.h"
#include "hal_early.h"
#include "nrf54l_soc_entry.h"

/* hal_early.c's, which the host link leaves out: it is the reset handler. */
volatile uint32_t tfw_cm_reset_mark;

void tfw_nrf54l_model_soc_early(void)
{
    tfw_cm_soc_early();
}

void tfw_nrf54l_model_watchdog_arm(void)
{
    tfw_cm_soc_watchdog_arm();
}

void tfw_nrf54l_model_watchdog_pet(void)
{
    tfw_cm_soc_watchdog_pet();
}

uint32_t tfw_nrf54l_model_mark(void)
{
    return tfw_cm_reset_mark;
}

void tfw_nrf54l_model_mark_clear(void)
{
    tfw_cm_reset_mark = 0u;
}

void tfw_nrf54l_model_poke(uintptr_t addr, uint32_t val)
{
    TFW_CM_REG(addr) = val;
}
