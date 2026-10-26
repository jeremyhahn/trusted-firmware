/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mcxn94x SoC's reset cause: CMC0's SRS read and decoded to the HAL's
 * values. The register is reached through a pointer the caller names
 * (hal_soc.c passes &TFW_MCXN_CMC_SRS), so a host test can hand it a fake
 * word (test/unit/test_mcxn94x_reset.c).
 */
#ifndef TFW_CM_MCXN94X_SOC_RESET_H
#define TFW_CM_MCXN94X_SOC_RESET_H

#include <stdint.h>

#include "tfw/types.h"

/* The cause an SRS value records: one source the HAL has a value for, alone
 * or beside the WARM and FATAL class flags, is that cause; anything else, a
 * source the HAL has no value for, two sources at once or none, is
 * TFW_RESET_UNKNOWN. requested is nonzero when the reset handler found
 * TFW_CM_MARK_RESETREQ (src/hal/cortex-m/hal_early.h): the CPU's reset
 * request is the firmware's own restart only then. */
tfw_reset_cause_t tfw_mcxn_reset_decode(uint32_t srs, uint8_t requested);

/* Reads *srs and returns its cause. SRS is read-only and holds the last
 * reset only (soc_reset.c), so there is nothing to clear. */
tfw_reset_cause_t tfw_mcxn_reset_take(const volatile uint32_t *srs, uint8_t requested);

#endif /* TFW_CM_MCXN94X_SOC_RESET_H */
