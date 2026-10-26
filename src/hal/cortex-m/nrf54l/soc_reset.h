/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's reset cause: RESET.RESETREAS read, decoded to the HAL's
 * values and cleared. The register is reached through a pointer the caller
 * names (hal_soc.c passes &TFW_NRF_RESETREAS), so a host test can hand it a
 * fake word (test/unit/test_nrf54l_reset.c).
 */
#ifndef TFW_CM_NRF54L_SOC_RESET_H
#define TFW_CM_NRF54L_SOC_RESET_H

#include <stdint.h>

#include "tfw/types.h"

/* The cause a RESETREAS value records: one cause the HAL has a value for,
 * alone, is that cause; nothing latched is power-on; anything else, a bit
 * the HAL has no value for or two causes at once, is TFW_RESET_UNKNOWN.
 * requested is nonzero when the reset handler found TFW_CM_MARK_RESETREQ
 * (src/hal/cortex-m/hal_early.h): the CPU's reset request is the firmware's
 * own restart only then. */
tfw_reset_cause_t tfw_nrf54l_reset_decode(uint32_t resetreas, uint8_t requested);

/* Reads *resetreas, clears every bit of it and returns the value's cause, so
 * the next reset is not reported as this one. */
tfw_reset_cause_t tfw_nrf54l_reset_take(volatile uint32_t *resetreas, uint8_t requested);

#endif /* TFW_CM_NRF54L_SOC_RESET_H */
