/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE HAL CONTRACT.
 *
 * Plain extern functions, resolved at LINK time. The Makefile links exactly one
 * implementation: src/hal/avr for a device build, test/mock for the host.
 *
 * Not a function-pointer vtable, and the reason is not style. A vtable of this
 * many entries costs 68 bytes of RAM on an ATtiny85 -- thirteen percent of its
 * SRAM -- and, worse, every slot is a live reference, so --gc-sections can no
 * longer drop the I2C, SPI, 1-Wire and tone routines from an image that never
 * calls them. Link-time substitution costs nothing, keeps -flto able to inline
 * ag_hal_gpio_write down to a two-instruction sbi, and makes the contract
 * enforced by the linker: add a function here, forget the mock, and `make test`
 * fails to link rather than failing in some later test nobody wrote yet.
 *
 * Everything that can fail returns ag_status_t. The four that cannot are the
 * trivial queries.
 *
 * This header grows as capabilities land. It is deliberately not the full
 * eventual surface up front: an unimplemented declaration is a link error in
 * both builds, so the contract only ever contains what actually exists.
 */
#ifndef AG_HAL_H
#define AG_HAL_H

#include <stdint.h>
#include "ag/status.h"
#include "ag/types.h"

/* ------------------------------------------------------------- lifecycle */

/* Brings the part to a defined state. MUST latch and clear the reset cause and
 * disarm the watchdog in its first instructions, before any C runtime work: a
 * watchdog reset that leaves WDE set re-triggers before main() is reached and
 * the part loops forever, bootlooping in a way that looks like a dead board. */
ag_status_t      ag_hal_init(void);

ag_reset_cause_t ag_hal_reset_cause(void);

/* Does not return. */
void             ag_hal_reset(void);

/* ------------------------------------------------------------------ time */

/* Milliseconds since boot. On the device this reads a counter an ISR writes,
 * so the multi-byte read is wrapped in an atomic section -- and that section is
 * HERE, inside the HAL, which is why the scheduler above can be plain portable
 * C with no volatile and no knowledge that interrupts exist. */
ag_ms_t          ag_hal_millis(void);

/* Busy-waits. Callable ONLY from bus bit-banging, where the delay IS the
 * protocol -- 1-Wire slots, the USI clock, the ATECC608A wake pulse. Never
 * from a command handler: this firmware has no delay() and must not grow one. */
void             ag_hal_delay_us(uint16_t us);

/* ------------------------------------------------------------- byte link */

ag_status_t      ag_hal_uart_init(uint32_t baud);

/* 1 if a byte was taken, 0 if none was waiting. Never blocks. */
uint8_t          ag_hal_uart_rx(uint8_t *out);

/* 1 if queued, 0 if the ring is full. Never blocks: the transport applies
 * backpressure by keeping its emit cursor and trying again next loop, which is
 * what lets the scheduler keep running while a long response drains. */
uint8_t          ag_hal_uart_tx_try(uint8_t byte);

/* 1 when the ring is empty AND the shift register has finished. The deferred
 * reset waits on this, which is the principled version of the sleep the old
 * firmware used before rebooting and hoped was long enough. */
uint8_t          ag_hal_uart_tx_idle(void);

/* ------------------------------------------------------------------ gpio */
/* These take a RESOLVED pin, never a logical one. That is why the HAL is
 * identical code on every part in the family: it does no lookup, consults no
 * table, and knows nothing about which board it is on. */

ag_status_t      ag_hal_gpio_mode(const ag_pinref_t *p, ag_pin_mode_t mode);
ag_status_t      ag_hal_gpio_write(const ag_pinref_t *p, uint8_t level);
ag_status_t      ag_hal_gpio_read(const ag_pinref_t *p, uint8_t *out);

/* ------------------------------------------------------------------- adc */

typedef enum {
    AG_ADC_REF_VCC      = 0,
    AG_ADC_REF_EXTERNAL = 1,
    AG_ADC_REF_INTERNAL = 2
} ag_adc_ref_t;

ag_status_t      ag_hal_adc_init(void);
ag_status_t      ag_hal_adc_set_ref(ag_adc_ref_t ref);
ag_status_t      ag_hal_adc_read(uint8_t channel, uint16_t *out_raw);

/* ------------------------------------------------------------ pwm / tone */

/* A pin resolved to an output-compare unit, the PWM analogue of ag_pinref_t.
 * The HAL takes this rather than a pin number for the same reason: it does no
 * lookup and is identical code on every part in the family. */
typedef struct {
    uint16_t tccra;    /* address of TCCRnA */
    uint16_t ocr;      /* address of OCRnx (low byte, for a 16-bit timer) */
    uint8_t  com_bit;  /* bit position of COMnx1 within TCCRnA */
    uint8_t  is16;     /* the timer is 16-bit, so OCR is a word */
} ag_pwmref_t;

ag_status_t      ag_hal_pwm_write(const ag_pwmref_t *p, uint8_t duty);
ag_status_t      ag_hal_pwm_stop(const ag_pwmref_t *p);

/* Square wave on a pin, for a piezo. hz of 0 stops it. */
ag_status_t      ag_hal_tone_start(const ag_pinref_t *p, uint16_t hz);
ag_status_t      ag_hal_tone_stop(void);

/* ----------------------------------------------------------------- pulse */

/* Measures one pulse of the given level, in microseconds. Returns
 * AG_ERR_TIMEOUT if none arrives within timeout_us. */
ag_status_t      ag_hal_pulse_in(const ag_pinref_t *p, uint8_t level,
                                 uint32_t timeout_us, uint32_t *out_us);

/* Captures up to max_edges edge-to-edge intervals, in microseconds.
 *
 * THE POINT OF THIS PRIMITIVE. A DHT22 distinguishes a bit by whether a pulse
 * is 26 or 70 microseconds long -- far faster than a serial round trip per bit,
 * so "the host decodes the sensor" could not otherwise mean anything. Capturing
 * the intervals and letting the host turn them into bits keeps sensor knowledge
 * off the device while still making timing-critical sensors work. The same
 * primitive reads DHT11/22, HC-SR04, IR remotes and RC PPM, with no
 * sensor-specific code in firmware at all. */
ag_status_t      ag_hal_pulse_capture(const ag_pinref_t *p, uint8_t start_level,
                                      uint16_t *out_us, uint8_t max_edges,
                                      uint32_t timeout_us, uint8_t *out_n);

/* ---------------------------------------------------------------- eeprom */

ag_status_t      ag_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len);
ag_status_t      ag_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len);

/* -------------------------------------------------------------- handoff */

/*
 * The AG_BOOT_HANDOFF_LEN bytes of SRAM ag-boot places its CDI handoff block
 * in (include/ag/boot.h), or the same span on a board or a boot with nothing
 * behind it -- there being no bootloader is not distinguished from "there is
 * a bootloader and it wrote nothing" at this layer; src/core/handoff.c's
 * magic/version/CRC check is what tells the two apart, the same way it tells
 * a corrupted block from a real one.
 *
 * Mutable and never NULL, because the reader owns clearing it once consumed
 * -- see boot.h for why the block must not outlive that one read. On the
 * device this is a fixed SRAM address the bootloader also names, placed
 * there at LINK time (mk/avr.mk); the mock returns a plain buffer a test can
 * pre-load.
 */
uint8_t          *ag_hal_handoff_block(void);

/* ------------------------------------------------------------------ buses */

ag_status_t      ag_hal_i2c_init(uint32_t hz);
/* One transaction: write wlen bytes, then read rlen. Either may be zero. */
ag_status_t      ag_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                                 uint8_t *rd, uint8_t rlen);

ag_status_t      ag_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first);
/* Full duplex: len bytes out, len bytes in, with cs asserted throughout. */
ag_status_t      ag_hal_spi_xfer(const ag_pinref_t *cs, const uint8_t *wr,
                                 uint8_t *rd, uint8_t len);

ag_status_t      ag_hal_ow_reset(const ag_pinref_t *p, uint8_t *out_presence);
ag_status_t      ag_hal_ow_write(const ag_pinref_t *p, const uint8_t *buf, uint8_t len);
ag_status_t      ag_hal_ow_read(const ag_pinref_t *p, uint8_t *buf, uint8_t len);

/* ---------------------------------------------------------------- system */

/* Bytes between the top of the heap and the current stack pointer. Reported by
 * SYS_INFO; a host watching it fall knows about a leak before the device dies
 * of one. */
uint16_t         ag_hal_free_ram(void);

#endif /* AG_HAL_H */
