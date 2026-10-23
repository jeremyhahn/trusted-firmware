/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/interrupt.h> for the host, as wide as the src/hal/avr units under test
 * need (#163). ISR(vector) defines an ordinary external function named for the
 * vector, so a suite runs an interrupt's body by calling it by that name -- the
 * board profile's TFW_UART_*_VECT, say -- with no seam in the unit. Only on the
 * -I of the suites that test those units; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_INTERRUPT_H
#define TFW_TEST_AVR_INTERRUPT_H

#define ISR(vector, ...) void vector(void); void vector(void)

/* src/hal/avr/hal_sys.c: the global interrupt flag, as calls the suite logs. */
void cli(void);
void sei(void);

#endif
