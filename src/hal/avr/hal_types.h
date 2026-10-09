/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The AVR half of the HAL's types: what a resolved pin, a resolved PWM channel
 * and the board's port and PWM rows LOOK LIKE on a classic AVR.
 *
 * include/tfw/hal.h includes "hal_types.h" and the build picks which one by
 * include path -- src/hal/<arch> for a device, test/mock for the host -- the
 * same link-time substitution the functions get. Core, the capabilities and the
 * protocol layer therefore see tfw_pinref_t and tfw_pwmref_t as OPAQUE: they
 * declare one, hand it to the board lookup to fill and hand it to the HAL to
 * use, and never name a field. Anything that does is reaching under the
 * contract, and a second architecture would break it.
 *
 * A pin is the address of its PINx register and a bit. On every classic AVR
 * DDRx is PINx+1 and PORTx is PINx+2, so this pair locates all three, which is
 * what lets the HAL do no lookup and be identical code on every part in the
 * family. A PWM channel is TCCRnA, OCRnx and the COMnx1 bit within TCCRnA.
 *
 * The port and PWM ROW types are here too, because a board's tables are data in
 * this architecture's shape (boards/<board>/board.c fills them positionally),
 * and tfw_pinref_resolve / tfw_pwmref_resolve are the only code that turns a row
 * into a reference. src/core/board_lookup.c calls them and so stays the same
 * lookup logic on every architecture.
 */
#ifndef TFW_HAL_TYPES_H
#define TFW_HAL_TYPES_H

#include <stdint.h>
#include "tfw/progmem.h"

typedef struct {
    uint16_t port;  /* address of PINx, as a memory-mapped SRAM address */
    uint8_t  bit;
} tfw_pinref_t;

typedef struct {
    uint16_t tccra;    /* address of TCCRnA */
    uint16_t ocr;      /* address of OCRnx (low byte, for a 16-bit timer) */
    uint8_t  com_bit;  /* bit position of COMnx1 within TCCRnA */
    uint8_t  is16;     /* the timer is 16-bit, so OCR is a word */
} tfw_pwmref_t;

/* One row of a board's port table. */
typedef struct {
    uint16_t pin_addr;  /* address of PINx; DDRx is +1 and PORTx is +2 on every classic AVR */
} tfw_port_desc_t;

/* One row of a board's PWM table. Same fields as tfw_pwmref_t; kept a distinct
 * type because one lives in flash and the other in RAM. */
typedef struct {
    uint16_t tccra;
    uint16_t ocr;
    uint8_t  com_bit;
    uint8_t  is16;
} tfw_pwm_desc_t;

/* A pin's coordinate in the board's pin table: a port index and a bit.
 *
 * Packed into one byte, port in the high nibble and bit in the low, because a
 * classic AVR has at most eleven ports of eight bits and the pin table is
 * flash on a part with none to spare -- a byte per pin more would not fit the
 * 32U4's element image. Another architecture defines its own, wider type; core
 * (src/core/board_lookup.c) reads a coordinate only through the three accessors
 * below and never shifts, masks or assumes a width.
 *
 * TFW_PORT_NONE as the port marks a pin with no digital port (ADC-only). */
typedef uint8_t tfw_pin_coord_t;

#define TFW_PORT_NONE     0x0Fu
#define TFW_PB(port, bit) ((tfw_pin_coord_t)(((port) << 4) | (bit)))

/* Reads a coordinate out of a flash-resident pin row. */
static inline tfw_pin_coord_t tfw_pin_coord_load(const tfw_pin_coord_t *row)
{
    return tfw_pgm_u8(row);
}

static inline uint8_t tfw_pin_coord_port(tfw_pin_coord_t c) { return (uint8_t)(c >> 4); }
static inline uint8_t tfw_pin_coord_bit(tfw_pin_coord_t c)  { return (uint8_t)(c & 0x0Fu); }

/* Row -> reference. The row is read through tfw_pgm_*: it lives in flash. */
static inline void tfw_pinref_resolve(tfw_pinref_t *out, const tfw_port_desc_t *port,
                                      uint8_t bit)
{
    out->port = tfw_pgm_u16(&port->pin_addr);
    out->bit  = bit;
}

static inline void tfw_pwmref_resolve(tfw_pwmref_t *out, const tfw_pwm_desc_t *row)
{
    out->tccra   = tfw_pgm_u16(&row->tccra);
    out->ocr     = tfw_pgm_u16(&row->ocr);
    out->com_bit = tfw_pgm_u8(&row->com_bit);
    out->is16    = tfw_pgm_u8(&row->is16);
}

#endif /* TFW_HAL_TYPES_H */
