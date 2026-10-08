/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The stub linked in place of bus.c on a board with none of I2C, SPI or
 * 1-Wire -- the ATtiny85 today.
 *
 * main.c calls tfw_bus_init() unconditionally, with no #if: the gate is which
 * of this file and bus.c the Makefile selects (mk/avr.mk's BUS_SRCS), exactly
 * the mechanism the three files under src/crypto use for tfw_crypto_init()
 * across the tiers. A board with no bus at all still needs the SYMBOL to
 * exist for main.c to link against; it just needs it to do nothing.
 */

#include "bus.h"

void tfw_bus_init(void)
{
}
