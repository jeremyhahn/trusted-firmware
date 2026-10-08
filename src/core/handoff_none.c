/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-handoff stub, linked instead of handoff.c on a board with no boot
 * section at all -- its profile.mk defines no RAM_END, see mk/avr.mk. There
 * being no bootloader means there is nothing to hand off and no fixed SRAM
 * address for one to have used in the first place; boards/attiny85/
 * profile.mk states the reason (no BOOTRST, no boot lock bits, no room).
 *
 * main.c calls tfw_boot_handoff_init and src/hal/avr/hal_sys.c calls
 * tfw_boot_handoff_reset_cause on EVERY board, so both stay real symbols here
 * -- just ones with no storage behind them. Everything else in this file
 * answers "no handoff" and, being referenced only from code paths this board's
 * feature set already compiles out (TFW_FEATURE_ATTEST is 0 here), is dropped
 * by --gc-sections rather than costing this part anything.
 */
#include "handoff.h"

void tfw_boot_handoff_init(void) {}

const uint8_t *tfw_boot_handoff_measurement(void)
{
    return 0;
}

uint8_t tfw_boot_handoff_reset_cause(uint8_t *out)
{
    (void)out;
    return 0u;
}

const uint8_t *tfw_boot_handoff_tier(void)
{
    return 0;
}

uint8_t tfw_boot_handoff_take_cdi(uint8_t out[TFW_BOOT_MEASURE_LEN])
{
    (void)out;
    return 0u;
}
