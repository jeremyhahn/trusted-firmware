/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The Cortex-M half of the HAL's types: what a resolved pin, a resolved PWM
 * channel and the board's port and PWM rows look like on a Cortex-M part.
 *
 * Found by include path exactly as src/hal/avr/hal_types.h is, and opaque to
 * core for the same reason. Nothing here is a register address: a port row
 * holds the SoC's index for the bank, and the SoC's hal_gpio.c
 * (src/hal/cortex-m/<soc>/) maps the index to its registers. An address would
 * not fit the host build's port key in any case (test/mock/hal_types.h).
 *
 * A PWM row has the host's four positional fields so a board table under
 * boards/ compiles against either; no Cortex-M board drives a compare unit
 * yet, so they carry no meaning here beyond that.
 */
#ifndef TFW_HAL_TYPES_H
#define TFW_HAL_TYPES_H

#include <stdint.h>
#include "tfw/progmem.h"

typedef struct {
    uint8_t port;      /* the SoC's bank index */
    uint8_t bit;
} tfw_pinref_t;

typedef struct {
    uint8_t unit;
    uint8_t channel;
    uint8_t port;
    uint8_t bit;
} tfw_pwmref_t;

/* One row of a board's port table: the SoC's index for the bank. */
typedef struct {
    uint8_t base;
} tfw_port_desc_t;

/* One row of a board's PWM table. Same fields as tfw_pwmref_t. */
typedef struct {
    uint8_t unit;
    uint8_t channel;
    uint8_t port;
    uint8_t bit;
} tfw_pwm_desc_t;

typedef struct {
    uint8_t port;
    uint8_t bit;
} tfw_pin_coord_t;

#define TFW_PORT_NONE     0xFFu
#define TFW_PB(port, bit) { (port), (bit) }

static inline tfw_pin_coord_t tfw_pin_coord_load(const tfw_pin_coord_t *row)
{
    return *row;
}

static inline uint8_t tfw_pin_coord_port(tfw_pin_coord_t c) { return c.port; }
static inline uint8_t tfw_pin_coord_bit(tfw_pin_coord_t c)  { return c.bit; }

static inline void tfw_pinref_resolve(tfw_pinref_t *out, const tfw_port_desc_t *port,
                                      uint8_t bit)
{
    out->port = port->base;
    out->bit  = bit;
}

static inline void tfw_pwmref_resolve(tfw_pwmref_t *out, const tfw_pwm_desc_t *row)
{
    out->unit    = row->unit;
    out->channel = row->channel;
    out->port    = row->port;
    out->bit     = row->bit;
}

#endif /* TFW_HAL_TYPES_H */
