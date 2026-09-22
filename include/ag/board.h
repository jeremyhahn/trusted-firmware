/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The board descriptor: pure data plus pure lookup.
 *
 * This layer sits ABOVE the HAL, not inside it, and that placement is the whole
 * design. It contains no register access -- only addresses stored as numbers --
 * so it compiles and runs on the host, which means the REAL ATmega328P pin
 * table is unit-tested on x86. A wrong port bit for pin 13 is otherwise a class
 * of bug findable only by wiring up a board and watching the wrong LED.
 *
 * Adding a board is therefore data: a header of constants and a table of pins.
 * If adding one ever requires touching src/, that is a defect in this design
 * rather than a property of the board.
 */
#ifndef AG_BOARD_H
#define AG_BOARD_H

#include <stdint.h>
#include "ag/status.h"
#include "ag/types.h"

#define AG_PORT_NONE 0x0Fu  /* the pin has no digital port (ADC-only) */
#define AG_ADC_NONE  0xFFu
#define AG_PWM_NONE  0xFFu
#define AG_PWM_RSVD  0xFEu  /* has an output-compare unit, but its timer is the system tick */

/* Packs a port index and a bit into one byte. */
#define AG_PB(port, bit) ((uint8_t)(((port) << 4) | (bit)))

/* Per-pin capability flags, as CAPS reports them. */
#define AG_PIN_DIGITAL  0x01u
#define AG_PIN_PWM      0x02u
#define AG_PIN_ADC      0x04u
#define AG_PIN_RESERVED 0x20u  /* /RESET, or claimed by a software peripheral */
#define AG_PIN_IN_USE   0x40u  /* claimed by this build: the UART pins, say */
#define AG_PIN_LED      0x80u

typedef struct {
    uint16_t pin_addr;  /* address of PINx; DDRx is +1 and PORTx is +2 on every classic AVR */
} ag_port_desc_t;

typedef struct {
    uint8_t port_bit;   /* AG_PB(port index, bit) */
    uint8_t adc_ch;     /* ADC channel, or AG_ADC_NONE */
    uint8_t pwm_idx;    /* index into the PWM table, or AG_PWM_NONE / AG_PWM_RSVD */
    uint8_t flags;
} ag_pin_desc_t;

/* Resolves a logical pin to hardware. Returns AG_ERR_BAD_PIN if the pin does
 * not exist on this board, and AG_ERR_UNSUPPORTED if it exists but has no
 * digital port -- an ADC-only input, which can be read and never written. */
ag_status_t ag_board_pin(ag_pin_t pin, ag_pinref_t *out);

/* The ADC channel behind a pin, or AG_ERR_UNSUPPORTED if it has none. */
ag_status_t ag_board_adc_channel(ag_pin_t pin, uint8_t *out);

/* Capability flags, for CAPS. Returns 0 for a pin that does not exist. */
uint8_t     ag_board_pin_flags(ag_pin_t pin);

/* How many logical pins this board has. */
uint8_t     ag_board_pin_count(void);

/* The board's name, for CAPS and for diagnostics. */
const char *ag_board_name(void);

#endif /* AG_BOARD_H */
