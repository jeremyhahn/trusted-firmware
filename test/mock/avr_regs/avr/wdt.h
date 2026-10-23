/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/wdt.h> for the host, as wide as the src/hal/avr units under test need
 * (#163). Declarations only: the suite defines each and counts the calls.
 * Only on the -I of the suites that test those units; the AVR build uses
 * avr-libc's.
 */
#ifndef TFW_TEST_AVR_WDT_H
#define TFW_TEST_AVR_WDT_H

void wdt_reset(void);

/* src/hal/avr/hal_sys.c: arm and disarm, with avr-libc's timeout codes. */
#define WDTO_15MS 0
#define WDTO_8S   9
void wdt_enable(unsigned char timeout);
void wdt_disable(void);

#endif
