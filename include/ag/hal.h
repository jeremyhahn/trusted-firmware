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

/* ---------------------------------------------------------------- system */

/* Bytes between the top of the heap and the current stack pointer. Reported by
 * SYS_INFO; a host watching it fall knows about a leak before the device dies
 * of one. */
uint16_t         ag_hal_free_ram(void);

#endif /* AG_HAL_H */
