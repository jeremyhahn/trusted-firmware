/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host implementation of the HAL, plus the controls a test needs to drive
 * it. Linked instead of src/hal/avr, with test/mock/hal_types.h standing in for
 * src/hal/avr/hal_types.h; the code under test cannot tell.
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
#ifndef TFW_HAL_MOCK_H
#define TFW_HAL_MOCK_H

#include <stddef.h>
#include <stdint.h>
#include "tfw/types.h"

void   tfw_mock_reset(void);

/* time travel */
void   tfw_mock_set_millis(tfw_ms_t t);
void   tfw_mock_advance(tfw_ms_t dt);

/* the byte link, from the test's side */
void   tfw_mock_feed_rx(const uint8_t *b, size_t n);
size_t tfw_mock_drain_tx(uint8_t *out, size_t cap);
size_t tfw_mock_tx_pending(void);
void   tfw_mock_set_tx_capacity(size_t n);   /* 0 = unlimited; models a full ring */

/* gpio: per-port state keyed by the board descriptor's opaque port value, so a
 * test can read back what the code under test wrote and assert on the pin
 * rather than on the call. The mock models pins, not registers: see
 * test/mock/hal_types.h. */
uint8_t tfw_mock_pin_level(uint16_t port, uint8_t bit);
uint8_t tfw_mock_pin_is_output(uint16_t port, uint8_t bit);
uint8_t tfw_mock_pin_pullup(uint16_t port, uint8_t bit);
void    tfw_mock_set_input_level(uint16_t port, uint8_t bit, uint8_t level);

/* adc: a test sets what the converter will report for a channel */
void tfw_mock_set_adc(uint8_t channel, uint16_t raw);

/* eeprom: a flat array the test can inspect directly */
uint8_t *tfw_mock_eeprom(void);
size_t   tfw_mock_eeprom_size(void);

/* handoff: the raw TFW_BOOT_HANDOFF_LEN-byte block tfw_hal_handoff_block()
 * returns. Zeroed by tfw_mock_reset(), which reads as "no handoff" -- the same
 * thing a stock bootloader or cold SRAM produces. A test builds a real one
 * with tfw_boot_handoff_build (boot/boot_handoff.c) and copies it in here. */
uint8_t *tfw_mock_handoff(void);

/* pwm: the duty a pin was last given, and whether it is connected at all. The
 * arguments are the board descriptor's opaque duty and control keys. */
uint8_t tfw_mock_pwm_duty(uint16_t duty_key);
uint8_t tfw_mock_pwm_connected(uint16_t ctl_key, uint8_t bit);
uint16_t tfw_mock_tone_hz(void);

/* pulse: the intervals the next capture will report */
void tfw_mock_set_pulse_in(uint32_t us);
void tfw_mock_set_capture(const uint16_t *us, uint8_t n);

/* buses: the bytes a transfer will return, and what it was asked to send */
void    tfw_mock_set_bus_response(const uint8_t *b, uint8_t n);
uint8_t tfw_mock_last_i2c_addr(void);
uint8_t tfw_mock_ow_presence_set(uint8_t present);

/* buses: what the last I2C_CONFIG / SPI_CONFIG actually reached the HAL with,
 * so a test can tell "dispatched and validated" from "dispatched and ignored".
 * The reset value of each -- 0xFFFFFFFF for the clock, 0xFF for the SPI
 * fields -- is out of the range a real call could ever pass, so "never
 * called" cannot be mistaken for a real value, including zero. */
uint32_t tfw_mock_last_i2c_hz(void);
uint8_t  tfw_mock_last_spi_mode(void);
uint8_t  tfw_mock_last_spi_clkdiv(void);
uint8_t  tfw_mock_last_spi_lsb(void);

/* system */
void   tfw_mock_set_reset_cause(tfw_reset_cause_t c);
void   tfw_mock_set_free_ram(uint32_t n);
unsigned tfw_mock_reset_count(void);         /* how often tfw_hal_reset() was called */
uint8_t  tfw_mock_irq_enabled(void);         /* 1 once tfw_hal_irq_enable() was called */

#endif /* TFW_HAL_MOCK_H */
