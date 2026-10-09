/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's half of the HAL's types: the counterpart of src/hal/avr/hal_types.h,
 * found instead of it by include path.
 *
 * Nothing here is a register layout. A pin is an opaque KEY -- the board's port
 * value, used only to tell one port from another -- and a bit; a PWM channel is
 * a key for its control word, a key for its duty word, a bit and a width flag.
 * The mock never adds an offset to a key, never overlays one key on another and
 * never treats a key as an address: hal_mock.c keeps a record per key. That is
 * the point of the exercise -- a test of core and the capabilities that passes
 * here has not relied on how a classic AVR happens to lay out PINx, DDRx and
 * PORTx.
 *
 * The row types stay positional-compatible with the AVR ones because the board
 * tables under boards/ are filled positionally with the board profile's values;
 * on the host those values are the keys.
 */
#ifndef TFW_HAL_TYPES_H
#define TFW_HAL_TYPES_H

#include <stdint.h>
#include "tfw/progmem.h"

typedef struct {
    uint16_t port;   /* opaque key of the pin's port */
    uint8_t  bit;
} tfw_pinref_t;

typedef struct {
    uint16_t ctl;    /* opaque key of the channel's control word */
    uint16_t duty;   /* opaque key of the channel's duty word */
    uint8_t  bit;    /* the control bit that connects the channel to its pin */
    uint8_t  wide;   /* the duty word is 16 bits */
} tfw_pwmref_t;

typedef struct {
    uint16_t key;
} tfw_port_desc_t;

typedef struct {
    uint16_t ctl;
    uint16_t duty;
    uint8_t  bit;
    uint8_t  wide;
} tfw_pwm_desc_t;

/* A pin's coordinate: a whole byte of port index and a whole byte of bit.
 *
 * Deliberately WIDER than the AVR's nibble packing (src/hal/avr/hal_types.h),
 * and the host build is tfwsim's architecture too: a part with 32-bit ports
 * or more than sixteen of them is what the pin page's whole-byte fields exist
 * for, and the host is where core is proved not to squeeze a coordinate back
 * into four bits. Positional, so TFW_PB() is an initializer that fits a board
 * table's row the way the AVR's byte does. */
typedef struct {
    uint8_t port;
    uint8_t bit;
} tfw_pin_coord_t;

#define TFW_PORT_NONE     0xFFu
#define TFW_PB(port, bit) { (port), (bit) }

static inline tfw_pin_coord_t tfw_pin_coord_load(const tfw_pin_coord_t *row)
{
    tfw_pin_coord_t c;

    c.port = tfw_pgm_u8(&row->port);
    c.bit  = tfw_pgm_u8(&row->bit);
    return c;
}

static inline uint8_t tfw_pin_coord_port(tfw_pin_coord_t c) { return c.port; }
static inline uint8_t tfw_pin_coord_bit(tfw_pin_coord_t c)  { return c.bit; }

static inline void tfw_pinref_resolve(tfw_pinref_t *out, const tfw_port_desc_t *port,
                                      uint8_t bit)
{
    out->port = tfw_pgm_u16(&port->key);
    out->bit  = bit;
}

static inline void tfw_pwmref_resolve(tfw_pwmref_t *out, const tfw_pwm_desc_t *row)
{
    out->ctl  = tfw_pgm_u16(&row->ctl);
    out->duty = tfw_pgm_u16(&row->duty);
    out->bit  = tfw_pgm_u8(&row->bit);
    out->wide = tfw_pgm_u8(&row->wide);
}

#endif /* TFW_HAL_TYPES_H */
