/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host implementation of the HAL, plus the controls a test needs to drive
 * it. Linked instead of src/hal/avr; the code under test cannot tell.
 *
 * This is test infrastructure, not a shipping port, which is why it lives under
 * test/ rather than src/hal/host/ -- src/ holds firmware and nothing else.
 *
 * Two of these controls are what make the whole scheduler testable. Time is a
 * variable a test sets, so "arm a five-second timer, advance 4999 ms, assert
 * still on, advance 2 ms, assert off" is four lines and runs instantly. The
 * millisecond-counter wrap, which takes 49.7 days to reach on real hardware,
 * is one assignment.
 */
#ifndef AG_HAL_MOCK_H
#define AG_HAL_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "ag/types.h"

void   ag_mock_reset(void);

/* time travel */
void   ag_mock_set_millis(ag_ms_t t);
void   ag_mock_advance(ag_ms_t dt);

/* the byte link, from the test's side */
void   ag_mock_feed_rx(const uint8_t *b, size_t n);
size_t ag_mock_drain_tx(uint8_t *out, size_t cap);
size_t ag_mock_tx_pending(void);
void   ag_mock_set_tx_capacity(size_t n);   /* 0 = unlimited; models a full ring */

/* gpio: a flat pseudo-register file, so a test can read back what the code
 * under test wrote and assert on the pin rather than on the call. */
uint8_t ag_mock_pin_level(uint16_t port, uint8_t bit);
uint8_t ag_mock_pin_is_output(uint16_t port, uint8_t bit);
uint8_t ag_mock_pin_pullup(uint16_t port, uint8_t bit);
void    ag_mock_set_input_level(uint16_t port, uint8_t bit, uint8_t level);

/* adc: a test sets what the converter will report for a channel */
void ag_mock_set_adc(uint8_t channel, uint16_t raw);

/* eeprom: a flat array the test can inspect directly */
uint8_t *ag_mock_eeprom(void);

/* system */
void   ag_mock_set_reset_cause(ag_reset_cause_t c);
void   ag_mock_set_free_ram(uint16_t n);
unsigned ag_mock_reset_count(void);         /* how often ag_hal_reset() was called */

#endif /* AG_HAL_MOCK_H */
