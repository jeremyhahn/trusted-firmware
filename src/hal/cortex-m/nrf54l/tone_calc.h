/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tone's TIMER arithmetic on nrf54l, kept apart from the register writes
 * (hal_tone.c) so a host test pins it alone (test/unit/test_nrf54l_tone.c).
 */
#ifndef TFW_CM_NRF54L_TONE_CALC_H
#define TFW_CM_NRF54L_TONE_CALC_H

#include <stdint.h>

#include "tfw/status.h"

/* The PRESCALER and CC[0] values for a square wave of hz on a TIMER clocked
 * at 16 MHz, with the GPIOTE pin toggled at every COMPARE0. */
static inline tfw_status_t tfw_nrf54l_tone_calc(uint32_t hz, uint32_t *prescaler, uint32_t *cc)
{
    (void)hz; (void)prescaler; (void)cc;
    return TFW_ERR_UNSUPPORTED; /* TODO: the arithmetic, next commit */
}

#endif /* TFW_CM_NRF54L_TONE_CALC_H */
