/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/sleep.h> for the host, one unit wide (#163): boot/hal/avr/recovery_halt.c's
 * power-down mode, avr-libc's value for the mega's SMCR, and sleep_mode() as a
 * call the suite takes. Only on the -I of the suite that tests that unit; the
 * AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_SLEEP_H
#define TFW_TEST_AVR_SLEEP_H

#include <avr/io.h>    /* SMCR, as avr-libc's header brings it */

#define SLEEP_MODE_PWR_DOWN (0x02 << 1)

void sleep_mode(void);

#endif
