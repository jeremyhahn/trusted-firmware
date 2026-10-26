/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stm32h5 SoC's reset cause: RCC_RSR read, decoded to the HAL's values
 * and cleared. The register is reached through a pointer the caller names
 * (hal_soc.c passes &TFW_H5_RCC_RSR), so a host test can hand it a fake word
 * (test/unit/test_stm32h5_reset.c).
 */
#ifndef TFW_CM_STM32H5_SOC_RESET_H
#define TFW_CM_STM32H5_SOC_RESET_H

#include <stdint.h>

#include "tfw/types.h"

/* The cause an RCC_RSR value records: one cause the HAL has a value for,
 * alone or beside the reset pin's flag, is that cause; the pin's flag alone
 * is the pin; anything else, a flag the HAL has no value for, two causes at
 * once or no flag at all, is TFW_RESET_UNKNOWN. requested is nonzero when
 * the reset handler found TFW_CM_MARK_RESETREQ (src/hal/cortex-m/hal_early.h):
 * the CPU's reset request is the firmware's own restart only then. */
tfw_reset_cause_t tfw_h5_reset_decode(uint32_t rsr, uint8_t requested);

/* Reads *rsr, writes RMVF to it, which clears every flag, and returns the
 * value's cause, so the next reset is not reported as this one. */
tfw_reset_cause_t tfw_h5_reset_take(volatile uint32_t *rsr, uint8_t requested);

#endif /* TFW_CM_STM32H5_SOC_RESET_H */
