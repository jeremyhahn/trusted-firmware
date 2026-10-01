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

/* handoff: the raw AG_BOOT_HANDOFF_LEN-byte block ag_hal_handoff_block()
 * returns. Zeroed by ag_mock_reset(), which reads as "no handoff" -- the same
 * thing a stock bootloader or cold SRAM produces. A test builds a real one
 * with ag_boot_handoff_build (boot/boot_handoff.c) and copies it in here. */
uint8_t *ag_mock_handoff(void);

/* pwm: the duty a pin was last given, and whether it is connected at all */
uint8_t ag_mock_pwm_duty(uint16_t ocr);
uint8_t ag_mock_pwm_connected(uint16_t tccra, uint8_t com_bit);
uint16_t ag_mock_tone_hz(void);

/* pulse: the intervals the next capture will report */
void ag_mock_set_pulse_in(uint32_t us);
void ag_mock_set_capture(const uint16_t *us, uint8_t n);

/* buses: the bytes a transfer will return, and what it was asked to send */
void    ag_mock_set_bus_response(const uint8_t *b, uint8_t n);
uint8_t ag_mock_last_i2c_addr(void);
uint8_t ag_mock_ow_presence_set(uint8_t present);

/* buses: what the last I2C_CONFIG / SPI_CONFIG actually reached the HAL with,
 * so a test can tell "dispatched and validated" from "dispatched and ignored".
 * The reset value of each -- 0xFFFFFFFF for the clock, 0xFF for the SPI
 * fields -- is out of the range a real call could ever pass, so "never
 * called" cannot be mistaken for a real value, including zero. */
uint32_t ag_mock_last_i2c_hz(void);
uint8_t  ag_mock_last_spi_mode(void);
uint8_t  ag_mock_last_spi_clkdiv(void);
uint8_t  ag_mock_last_spi_lsb(void);

/* system */
void   ag_mock_set_reset_cause(ag_reset_cause_t c);
void   ag_mock_set_free_ram(uint16_t n);
unsigned ag_mock_reset_count(void);         /* how often ag_hal_reset() was called */

#endif /* AG_HAL_MOCK_H */
