/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <util/delay.h> for the host, as wide as src/hal/avr/hal_pulse.c and
 * hal_bus.c need (#163). _delay_us() is a call to tfw_avr_delay_us(), which
 * the suite defines: it advances the suite's model clock by that many
 * microseconds, so a pin read after it sees the line as it is by then. Nothing
 * waits. Only on the -I of the suites that test those units; the AVR build
 * uses avr-libc's.
 */
#ifndef TFW_TEST_UTIL_DELAY_H
#define TFW_TEST_UTIL_DELAY_H

void tfw_avr_delay_us(double us);

#define _delay_us(us) tfw_avr_delay_us(us)

#endif
