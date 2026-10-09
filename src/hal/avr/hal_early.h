/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The latched reset cause, shared by the two images that both run hal_early.c.
 *
 * A variable and not a getter: .init3 runs before .bss is zeroed, so this is
 * written by code that cannot call anything, and a getter would add a symbol
 * whose only job is to return it.
 */
#ifndef TFW_HAL_EARLY_H
#define TFW_HAL_EARLY_H

#include <stdint.h>
#include "tfw/types.h"

/* MCUSR as it was at reset, latched and then cleared. Not valid before .init3,
 * which is to say not at any point a caller can observe. Callers want the
 * decoded cause below, not this register image. */
extern uint8_t tfw_hal_early_flags;

/* The latched MCUSR, decoded -- the one place either image turns the register
 * into the architecture-neutral enum. See hal_reset_decode.h. */
tfw_reset_cause_t tfw_hal_early_reset_cause(void);

#endif /* TFW_HAL_EARLY_H */
