/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The latched reset cause, shared by the two images that both run hal_early.c.
 *
 * A variable and not a getter: .init3 runs before .bss is zeroed, so this is
 * written by code that cannot call anything, and a getter would add a symbol
 * whose only job is to return it.
 */
#ifndef AG_HAL_EARLY_H
#define AG_HAL_EARLY_H

#include <stdint.h>

/* MCUSR as it was at reset, latched and then cleared. Not valid before .init3,
 * which is to say not at any point a caller can observe. */
extern uint8_t ag_hal_early_flags;

#endif /* AG_HAL_EARLY_H */
