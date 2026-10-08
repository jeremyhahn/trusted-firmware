/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Decodes a raw MCUSR byte into tfw_reset_cause_t.
 *
 * Pure and portable: no register access, just the bit positions the datasheet
 * defines for every classic AVR in this family (PORF=0, EXTRF=1, BORF=2,
 * WDRF=3 -- see the _Static_assert in src/hal/avr/hal_sys.c that pins these
 * against <avr/io.h>). One decoder, so src/hal/avr/hal_sys.c's own .init3
 * observation and a value handed off by tfw-boot (issue #18) are read the
 * same way instead of by two priority orders that can drift.
 *
 * Checked most-specific first: a brownout during a watchdog reset sets both
 * bits, and the brownout is the more useful thing to report -- it names a
 * power problem rather than a software one.
 */
#ifndef TFW_CORE_RESET_CAUSE_H
#define TFW_CORE_RESET_CAUSE_H

#include <stdint.h>
#include "tfw/types.h"

#define TFW_MCUSR_PORF  0x01u
#define TFW_MCUSR_EXTRF 0x02u
#define TFW_MCUSR_BORF  0x04u
#define TFW_MCUSR_WDRF  0x08u

tfw_reset_cause_t tfw_reset_cause_decode(uint8_t mcusr);

#endif /* TFW_CORE_RESET_CAUSE_H */
