/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Decodes a raw MCUSR byte into ag_reset_cause_t.
 *
 * Pure and portable: no register access, just the bit positions the datasheet
 * defines for every classic AVR in this family (PORF=0, EXTRF=1, BORF=2,
 * WDRF=3 -- see the _Static_assert in src/hal/avr/hal_sys.c that pins these
 * against <avr/io.h>). One decoder, so src/hal/avr/hal_sys.c's own .init3
 * observation and a value handed off by ag-boot (issue #18) are read the
 * same way instead of by two priority orders that can drift.
 *
 * Checked most-specific first: a brownout during a watchdog reset sets both
 * bits, and the brownout is the more useful thing to report -- it names a
 * power problem rather than a software one.
 */
#ifndef AG_CORE_RESET_CAUSE_H
#define AG_CORE_RESET_CAUSE_H

#include <stdint.h>
#include "ag/types.h"

#define AG_MCUSR_PORF  0x01u
#define AG_MCUSR_EXTRF 0x02u
#define AG_MCUSR_BORF  0x04u
#define AG_MCUSR_WDRF  0x08u

ag_reset_cause_t ag_reset_cause_decode(uint8_t mcusr);

#endif /* AG_CORE_RESET_CAUSE_H */
