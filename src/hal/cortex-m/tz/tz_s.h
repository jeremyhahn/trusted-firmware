/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 secure start-up's seams (#590): what a SoC file provides, and the
 * SAU programmer tz_s_main.c runs over tz_sau_regions.
 */
#ifndef TFW_TZ_S_H
#define TFW_TZ_S_H

#include <stddef.h>
#include "tz_regions.h"

/* What the SoC needs before the SAU before Non-secure code can run from the
 * Non-secure regions (mps2: tz_soc_mps2.c). Weak no-op by default. */
void tz_soc_open(void);

void tz_s_sau_program(const struct tz_sau_region *r, size_t n);

/* Initialises the DICE service from its inputs (tz_veneer.c); nonzero when it
 * holds keys to serve. */
int tz_s_dice_start(void);

#endif
