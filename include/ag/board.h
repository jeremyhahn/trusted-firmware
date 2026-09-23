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
#include "ag/hal.h"
#include "ag/config.h"

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
    uint16_t tccra;
    uint16_t ocr;
    uint8_t  com_bit;
    uint8_t  is16;
} ag_pwm_desc_t;

typedef struct {
    uint8_t port_bit;   /* AG_PB(port index, bit) */
    uint8_t adc_ch;     /* ADC channel, or AG_ADC_NONE */
    uint8_t pwm_idx;    /* index into the PWM table, or AG_PWM_NONE / AG_PWM_RSVD */
    uint8_t flags;
} ag_pin_desc_t;

/*
 * The three tables every board supplies, and the ONLY thing a board supplies.
 *
 * The lookups below are identical on every classic AVR -- same packing, same
 * PROGMEM reads, same bounds -- so they live once in src/core/board.c and the
 * board directories hold nothing but data. Before this split each board carried
 * its own copy of ninety lines of lookup, which is ninety lines that can differ
 * between two boards for no reason anyone intended.
 *
 * Declared with an unspecified length on purpose: the counts come from the
 * board profile, and a boardless translation unit including this header must
 * not need them. The lengths are checked where they can be -- a _Static_assert
 * beside each definition, which catches the table that has one fewer row than
 * its AG_PIN_COUNT says and would otherwise zero-fill in silence and hand a
 * host a pin mapped to port 0 bit 0.
 */
extern const ag_port_desc_t ag_board_ports[];
extern const ag_pin_desc_t  ag_board_pins[];
extern const ag_pwm_desc_t  ag_board_pwms[];

/* Resolves a logical pin to hardware. Returns AG_ERR_BAD_PIN if the pin does
 * not exist on this board, and AG_ERR_UNSUPPORTED if it exists but has no
 * digital port -- an ADC-only input, which can be read and never written. */
ag_status_t ag_board_pin(ag_pin_t pin, ag_pinref_t *out);

/* The ADC channel behind a pin, or AG_ERR_UNSUPPORTED if it has none. */
ag_status_t ag_board_adc_channel(ag_pin_t pin, uint8_t *out);

/* Resolves a pin to its output-compare unit. Returns AG_ERR_UNSUPPORTED when
 * the pin has none, and when its unit belongs to the timer driving the system
 * tick -- the board descriptor distinguishes those two, because "no PWM here"
 * and "this build spent it on the clock" are different facts and only one of
 * them might change. */
ag_status_t ag_board_pwm(ag_pin_t pin, ag_pwmref_t *out);

/* The pin's port index and bit, packed as the descriptor stores them: port in
 * the high nibble, bit in the low. AG_PORT_NONE in the high nibble means the
 * pin has no digital port at all. Returns 0xFF for a pin beyond the board.
 *
 * CAPS reports this byte verbatim. It cannot be derived from ag_board_pin(),
 * whose ag_pinref_t carries the PINx MEMORY ADDRESS rather than a port index --
 * the index is not recoverable from it, which is how the first version of the
 * pin page came to report a hardcoded high nibble for every pin on every board.
 */
uint8_t     ag_board_pin_port_bit(ag_pin_t pin);

/* Three states, because two would lose the one a host acts on:
 *
 *   0  the pin has no output-compare unit
 *   1  it has one and this build can drive it
 *   2  it has one, and this build spent that timer on the millisecond tick
 *
 * A host that cannot tell 0 from 2 will move a load to a different pin when the
 * right fix is a different build. ag_board_pwm() deliberately folds both
 * refusals into AG_ERR_UNSUPPORTED, so the distinction has to come from here.
 */
uint8_t     ag_board_pin_pwm_state(ag_pin_t pin);

/* Capability flags, for CAPS. Returns 0 for a pin that does not exist. */
uint8_t     ag_board_pin_flags(ag_pin_t pin);

/* How many logical pins this board has. */
uint8_t     ag_board_pin_count(void);

/* The board's name, for CAPS and for diagnostics. */
const char *ag_board_name(void);

#endif /* AG_BOARD_H */
