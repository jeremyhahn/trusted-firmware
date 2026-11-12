/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The board descriptor: pure data plus pure lookup.
 *
 * This layer sits ABOVE the HAL, not inside it, and that placement is the whole
 * design. It contains no register access -- only the architecture's descriptor rows --
 * so it compiles and runs on the host, which means the REAL ATmega328P pin
 * table is unit-tested on x86. A wrong port bit for pin 13 is otherwise a class
 * of bug findable only by wiring up a board and watching the wrong LED.
 *
 * Adding a board is therefore data: a header of constants and a table of pins.
 * If adding one ever requires touching src/, that is a defect in this design
 * rather than a property of the board.
 */
#ifndef TFW_BOARD_H
#define TFW_BOARD_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "tfw/config.h"

#define TFW_ADC_NONE  0xFFu
#define TFW_PWM_NONE  0xFFu
#define TFW_PWM_RSVD  0xFEu  /* has an output-compare unit, but its timer is the system tick */

/* A pin's coordinate -- tfw_pin_coord_t, TFW_PB(port, bit) and TFW_PORT_NONE --
 * is the architecture's, in hal_types.h: how wide a port index and a bit are,
 * and how they pack, depends on the silicon. */

/* Per-pin capability flags, as CAPS reports them. */
#define TFW_PIN_DIGITAL  0x01u
#define TFW_PIN_PWM      0x02u
#define TFW_PIN_ADC      0x04u
#define TFW_PIN_RESERVED 0x20u  /* /RESET, or claimed by a software peripheral */
#define TFW_PIN_IN_USE   0x40u  /* claimed by this build: the UART pins, say */
#define TFW_PIN_LED      0x80u

/* IN_USE in an Ethernet image, for the Ethernet shield's pins -- its two chip
 * selects and the SPI lines -- and nothing in any other image. A board table
 * ORs it into those rows, so GPIO, the scheduler, PWM, tone and SPI_XFER refuse
 * them exactly when the controller owns them, and a serial image's CAPS is
 * byte-for-byte what it was. An expression, not an #if, so the rows read the
 * same in every build. */
#define TFW_PIN_ETH      ((TFW_FEATURE_ETHERNET) ? TFW_PIN_IN_USE : 0u)
/* The OTA store's chip select (TFW_OTA_STORE_CS_PIN), taken the same way by an
 * image that links the stager (issue #75): driving it low would put the NOR on
 * the shared bus in the middle of someone else's transfer. */
#define TFW_PIN_OTA_STORE ((TFW_FEATURE_OTA_STAGE) ? TFW_PIN_IN_USE : 0u)
/* An I2C master's SDA and SCL, taken the same way by a board whose bus is a
 * build row (rp2350-pico2w's I2C=1): GPIO_MODE or a scheduled toggle would
 * hand the pin back to SIO and cut the bus. */
#define TFW_PIN_I2C      ((TFW_FEATURE_I2C) ? TFW_PIN_IN_USE : 0u)

/* tfw_port_desc_t and tfw_pwm_desc_t -- one row of the port table and of the PWM
 * table -- are the architecture's, in hal_types.h (reached through tfw/hal.h):
 * what a port or a PWM channel IS depends on the silicon. The pin table below
 * is not: a pin is a coordinate, an ADC channel, a PWM index and flags on any
 * architecture; only the coordinate's width is the architecture's. */

typedef struct {
    tfw_pin_coord_t port_bit;   /* TFW_PB(port index, bit) */
    uint8_t adc_ch;     /* ADC channel, or TFW_ADC_NONE */
    uint8_t pwm_idx;    /* index into the PWM table, or TFW_PWM_NONE / TFW_PWM_RSVD */
    uint8_t flags;
} tfw_pin_desc_t;

/*
 * The three tables every board supplies, and the ONLY thing a board supplies.
 *
 * The lookups below are identical on every board -- same rows, same flash
 * reads, same bounds, the coordinate read through the architecture's accessors -- so they live once in src/core/board_lookup.c and the
 * board directories hold nothing but data. Before this split each board carried
 * its own copy of ninety lines of lookup, which is ninety lines that can differ
 * between two boards for no reason anyone intended.
 *
 * Declared with an unspecified length on purpose: the counts come from the
 * board profile, and a boardless translation unit including this header must
 * not need them. The lengths are checked where they can be -- a _Static_assert
 * beside each definition, which catches the table that has one fewer row than
 * its TFW_PIN_COUNT says and would otherwise zero-fill in silence and hand a
 * host a pin mapped to port 0 bit 0.
 */
extern const tfw_port_desc_t tfw_board_ports[];
extern const tfw_pin_desc_t  tfw_board_pins[];
extern const tfw_pwm_desc_t  tfw_board_pwms[];

/* Resolves a logical pin to hardware. Returns TFW_ERR_BAD_PIN if the pin does
 * not exist on this board, and TFW_ERR_UNSUPPORTED if it exists but has no
 * digital port -- an ADC-only input, which can be read and never written. */
tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out);

/* The ADC channel behind a pin, or TFW_ERR_UNSUPPORTED if it has none. */
tfw_status_t tfw_board_adc_channel(tfw_pin_t pin, uint8_t *out);

/* Resolves a pin to its output-compare unit. Returns TFW_ERR_UNSUPPORTED when
 * the pin has none, and when its unit belongs to the timer driving the system
 * tick -- the board descriptor distinguishes those two, because "no PWM here"
 * and "this build spent it on the clock" are different facts and only one of
 * them might change. */
tfw_status_t tfw_board_pwm(tfw_pin_t pin, tfw_pwmref_t *out);

/* Three states, because two would lose the one a host acts on:
 *
 *   0  the pin has no output-compare unit
 *   1  it has one and this build can drive it
 *   2  it has one, and this build spent that timer on the millisecond tick
 *
 * A host that cannot tell 0 from 2 will move a load to a different pin when the
 * right fix is a different build. tfw_board_pwm() deliberately folds both
 * refusals into TFW_ERR_UNSUPPORTED, so the distinction has to come from here.
 */
uint8_t     tfw_board_pin_pwm_state(tfw_pin_t pin);

/* The CAPS pin page (#12), six bytes: pin, flags, port, bit, adc channel, pwm
 * state. Port and bit are a whole byte each on the wire, so a part with 32-bit
 * ports or more than sixteen of them fits; a pin with no digital port carries
 * TFW_WIRE_PORT_NONE in both. The descriptor's coordinate is read only through
 * the architecture's accessors (hal_types.h), so the page carries whatever port
 * and bit the architecture stores -- a nibble on the AVR, a byte elsewhere.
 *
 * This is also the only place a pin's port INDEX leaves the firmware: it cannot
 * be derived from tfw_board_pin(), whose tfw_pinref_t is the architecture's
 * opaque reference rather than an index, which is how the first version of the
 * pin page came to report a hardcoded port for every pin on every board.
 *
 * Built in one place so CAPS writes it with a single tfw_write_blob rather
 * than six calls. Returns TFW_ERR_BAD_PIN, writing nothing, for a pin beyond
 * the board. */
#define TFW_PIN_PAGE_LEN   6u
#define TFW_WIRE_PORT_NONE 0xFFu
tfw_status_t tfw_board_pin_page(tfw_pin_t pin, uint8_t *out);

/* Capability flags, for CAPS. Returns 0 for a pin that does not exist. */
uint8_t     tfw_board_pin_flags(tfw_pin_t pin);

/* How many logical pins this board has. */
uint8_t     tfw_board_pin_count(void);

/* The board's name, for CAPS and for diagnostics. */
const char *tfw_board_name(void);

#endif /* TFW_BOARD_H */
