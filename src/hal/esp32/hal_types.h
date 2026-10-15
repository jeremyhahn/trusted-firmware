/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ESP32 half of the HAL's types: what a resolved pin, a resolved PWM
 * channel and the board's port and PWM rows look like on an ESP-IDF part.
 *
 * Found by include path exactly as src/hal/avr/hal_types.h is, and opaque to
 * core for the same reason. Nothing here is a register address: on this port
 * the HAL drives the part through ESP-IDF's drivers, which take GPIO NUMBERS,
 * so a pin is a GPIO number and nothing else.
 *
 * A port is a bank of thirty-two GPIOs and its row holds the first GPIO number
 * in the bank, so a coordinate (port, bit) is GPIO base + bit -- the same
 * whole-byte coordinate the host build uses (test/mock/hal_types.h), because an
 * ESP32-P4 has fifty-five GPIOs and a nibble cannot index them.
 *
 * A PWM row is an LEDC channel, the GPIO it drives, the LEDC timer behind it
 * and the speed mode. Four fields, positional, the same count as the host's
 * row, so a board table under boards/ compiles against either.
 */
#ifndef TFW_HAL_TYPES_H
#define TFW_HAL_TYPES_H

#include <stdint.h>
#include "tfw/progmem.h"

typedef struct {
    uint8_t gpio;
} tfw_pinref_t;

typedef struct {
    uint8_t channel;   /* LEDC channel */
    uint8_t gpio;      /* the pin the channel drives */
    uint8_t timer;     /* LEDC timer the channel counts on */
    uint8_t mode;      /* LEDC speed mode: the original ESP32 has two, later parts one */
} tfw_pwmref_t;

/* One row of a board's port table: the first GPIO number of the bank. */
typedef struct {
    uint8_t base;
} tfw_port_desc_t;

/* One row of a board's PWM table. Same fields as tfw_pwmref_t. */
typedef struct {
    uint8_t channel;
    uint8_t gpio;
    uint8_t timer;
    uint8_t mode;
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
    out->gpio = (uint8_t)(port->base + bit);
}

static inline void tfw_pwmref_resolve(tfw_pwmref_t *out, const tfw_pwm_desc_t *row)
{
    out->channel = row->channel;
    out->gpio    = row->gpio;
    out->timer   = row->timer;
    out->mode    = row->mode;
}

#endif /* TFW_HAL_TYPES_H */
