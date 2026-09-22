/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include <util/atomic.h>

#include "ag/hal.h"

/*
 * On every classic AVR the three port registers are consecutive: PINx, then
 * DDRx, then PORTx. One address locates all three, which is what lets the board
 * descriptor store a single number per port and this file work unchanged from
 * an ATtiny to an ATmega2560.
 *
 * _SFR_MEM8 rather than _SFR_IO8 because the address is a runtime value from a
 * table, not a compile-time constant. The low I/O registers are also
 * memory-mapped at io+0x20, so a uniform 16-bit address reaches PORTB on an
 * ATtiny85 and PORTL on a 2560 alike -- which removes what would otherwise be a
 * conditional between narrow-I/O and extended-I/O parts.
 */
#define PIN_REG(p)  _SFR_MEM8((p)->port)
#define DDR_REG(p)  _SFR_MEM8((p)->port + 1u)
#define PORT_REG(p) _SFR_MEM8((p)->port + 2u)

/*
 * Every write below is read-modify-write on a shared register, so it must be
 * atomic. Two pins on the same port are two bits of one byte: an interrupt
 * landing between the read and the write loses whatever the ISR changed.
 *
 * The compiler turns a constant bit on a low-I/O port into a single sbi/cbi,
 * which is already atomic and the guard costs nothing. It cannot do that for a
 * runtime bit index, and this firmware resolves pins from a table -- so the
 * guard is doing real work here rather than being defensive decoration.
 */
ag_status_t ag_hal_gpio_mode(const ag_pinref_t *p, ag_pin_mode_t mode)
{
    uint8_t mask = (uint8_t)(1u << p->bit);

    switch (mode) {
    case AG_PIN_OUTPUT:
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            DDR_REG(p) |= mask;
        }
        return AG_OK;

    case AG_PIN_INPUT:
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            DDR_REG(p)  = (uint8_t)(DDR_REG(p)  & (uint8_t)~mask);
            PORT_REG(p) = (uint8_t)(PORT_REG(p) & (uint8_t)~mask);  /* pull-up off */
        }
        return AG_OK;

    case AG_PIN_INPUT_PULLUP:
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            DDR_REG(p)   = (uint8_t)(DDR_REG(p) & (uint8_t)~mask);
            PORT_REG(p) |= mask;
        }
        return AG_OK;

    default:
        return AG_ERR_RANGE;
    }
}

ag_status_t ag_hal_gpio_write(const ag_pinref_t *p, uint8_t level)
{
    uint8_t mask = (uint8_t)(1u << p->bit);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (level) {
            PORT_REG(p) |= mask;
        } else {
            PORT_REG(p) = (uint8_t)(PORT_REG(p) & (uint8_t)~mask);
        }
    }
    return AG_OK;
}

/* Reads PINx, not PORTx. PORTx is the output latch -- reading it on an input
 * pin returns the pull-up setting rather than the voltage on the wire, which
 * is a classic way to get a reading that is stable, plausible and wrong. */
ag_status_t ag_hal_gpio_read(const ag_pinref_t *p, uint8_t *out)
{
    *out = (uint8_t)((PIN_REG(p) & (uint8_t)(1u << p->bit)) != 0u);
    return AG_OK;
}
