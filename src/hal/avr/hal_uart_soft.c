/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The byte link for a part with NO USART.
 *
 * It implements exactly the same four functions hal_uart.c does, with the same
 * non-blocking contract, and the Makefile links one or the other -- never both.
 * The gate is file selection off AG_UART_SOFT in the board profile, so an image
 * for a part with a USART contains none of this and an image for a part without
 * one contains no reference to a register that does not exist.
 *
 * WHY THIS IS NOT A LOOP OF _delay_us. A bit-banged UART that busy-waits owns
 * the CPU for the length of a frame -- 521 us at 19200 -- and this firmware's
 * whole argument is that no command blocks the superloop. It would also be
 * half-duplex, and the transport sends an unsolicited BOOT event while a host
 * may already be talking. So the bit clock is a timer and both directions are
 * interrupt-driven, exactly as the hardware USART is.
 *
 * THE TIMER, AND THE TRICK THAT MAKES ONE TIMER ENOUGH.
 *
 * Timer1 free-runs in CTC with OCR1C as TOP and a period of exactly one bit.
 * Two compare units then live inside that period:
 *
 *   OCR1A  the transmitter's bit clock. Its phase is arbitrary -- a
 *          transmitter only has to put bits out one bit-time apart, and any
 *          phase does that.
 *   OCR1B  the receiver's sampling clock. Its phase is NOT arbitrary: it must
 *          land in the middle of each bit. So the pin-change ISR that sees the
 *          start edge writes OCR1B = TCNT1 + half a bit, and every compare
 *          after that is one full period later by construction.
 *
 * That is what lets a part with one spare timer transmit and receive at the
 * same time on unrelated phases. A design that re-started the timer on each
 * start bit would have to stop the transmitter to do it.
 *
 * THE ERROR BUDGET is in the board profile beside AG_UART_BAUD, because the
 * baud is a board fact. What is asserted HERE is that the numbers the board
 * chose actually produce that baud: the counter range and the divisor error are
 * compile-time checks, so a board that changes F_CPU finds out at build time
 * rather than on a scope.
 *
 * This file lives under src/hal/avr, one of the four places a preprocessor
 * conditional is permitted. It contains none.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "ag/hal.h"
#include "ag/config.h"

/* ------------------------------------------------------------------ rings */
/*
 * Two single-producer/single-consumer rings, identical in design to the
 * hardware UART's and for the same reason: each index is written by exactly one
 * side, a byte-wide load or store on AVR is one instruction and therefore
 * atomic, so no lock is needed anywhere. The asserts are not decoration -- at
 * 512 bytes an index would be two bytes wide, the single-instruction property
 * would evaporate, and the reader could observe a half-updated index.
 */
#ifndef AG_UART_RX_RING
#define AG_UART_RX_RING 64u
#endif
#ifndef AG_UART_TX_RING
#define AG_UART_TX_RING 64u
#endif

_Static_assert((AG_UART_RX_RING & (AG_UART_RX_RING - 1u)) == 0u,
               "AG_UART_RX_RING must be a power of two: the index wraps by masking");
_Static_assert((AG_UART_TX_RING & (AG_UART_TX_RING - 1u)) == 0u,
               "AG_UART_TX_RING must be a power of two: the index wraps by masking");
_Static_assert(AG_UART_RX_RING <= 256u && AG_UART_TX_RING <= 256u,
               "ring indices must fit in a uint8_t, or the lock-free single-writer "
               "property depends on a multi-byte store being atomic, which it is not");

static volatile uint8_t rx_buf[AG_UART_RX_RING];
static volatile uint8_t rx_head, rx_tail;   /* head: ISR writes. tail: main writes. */

static volatile uint8_t tx_buf[AG_UART_TX_RING];
static volatile uint8_t tx_head, tx_tail;   /* head: main writes. tail: ISR writes. */

/* ------------------------------------------------------------- bit timing */
/*
 * Derived from three board numbers and checked here, so the board states intent
 * and this file states whether the silicon can honour it.
 *
 * TCCR1's CS13:0 field encodes divisor 1 << (CS - 1): CS=1 is CK/1, CS=2 is
 * CK/2, and so on to CK/16384. Written that way rather than as a table because
 * the relation is exact and a table is one more thing to get wrong.
 */
#define UART_DIV       (1uL << (AG_UART_SOFT_CS - 1u))
#define UART_TICK_HZ   (F_CPU / UART_DIV)

/* Rounded to nearest, not truncated. Truncation would bias every board's baud
 * high by up to a whole tick, which at 100-odd ticks a bit is most of the error
 * budget spent on an avoidable choice. */
#define UART_BIT_TICKS ((UART_TICK_HZ + (AG_UART_BAUD / 2uL)) / AG_UART_BAUD)

_Static_assert(UART_BIT_TICKS >= 16uL,
               "too few timer ticks per bit to place a sample in the middle of one: "
               "LOWER AG_UART_SOFT_CS, because a coarser prescaler cannot resolve this baud");
_Static_assert(UART_BIT_TICKS <= 256uL,
               "more timer ticks per bit than OCR1C can express: raise AG_UART_SOFT_CS");

/*
 * THE DIVISOR ERROR, asserted rather than commented.
 *
 * The integer tick count is never exactly the ideal one, and the difference is
 * a fixed fraction of every bit -- so it accumulates across a frame and is spent
 * against the 50% sampling window. 2% is the ceiling because the rest of the
 * budget (the RC oscillator, the ISR jitter) needs the room; a board whose
 * numbers cannot do better than that has chosen the wrong baud, and this says
 * so at build time.
 *
 * Computed in parts-per-million on the TICK counts, which avoids both the
 * rounding of an integer baud and any need for floating point.
 */
#define UART_IDEAL_TICKS_MILLI ((uint64_t)UART_TICK_HZ * 1000uLL / (uint64_t)AG_UART_BAUD)
#define UART_ACTUAL_TICKS_MILLI ((uint64_t)UART_BIT_TICKS * 1000uLL)
#define UART_ERR_PPM \
    (((UART_ACTUAL_TICKS_MILLI > UART_IDEAL_TICKS_MILLI) \
        ? (UART_ACTUAL_TICKS_MILLI - UART_IDEAL_TICKS_MILLI) \
        : (UART_IDEAL_TICKS_MILLI - UART_ACTUAL_TICKS_MILLI)) * 1000000uLL \
     / UART_IDEAL_TICKS_MILLI)

_Static_assert(UART_ERR_PPM <= 20000uLL,
               "the bit time this board's F_CPU, prescaler and baud produce is more than "
               "2% from nominal, which leaves nothing for the oscillator and the ISR jitter");

/* ------------------------------------------------------------------- pins */
/* Both lines are on the one port the board's descriptor already names, so the
 * bits come from the board and the port does not need to. */
#define TX_MASK ((uint8_t)_BV(AG_UART_SOFT_TX_BIT))
#define RX_MASK ((uint8_t)_BV(AG_UART_SOFT_RX_BIT))

_Static_assert(AG_UART_SOFT_TX_BIT != AG_UART_SOFT_RX_BIT,
               "the soft UART's two lines are the same pin");

/* ------------------------------------------------------------ transmitter */

/* The frame as it will be shifted out, LSB first: bit 0 is the start bit (0),
 * bits 1..8 the data, bit 9 the stop bit (1). Holding it as one word means the
 * ISR has no notion of which part of a frame it is in -- it shifts and emits,
 * and the framing is in the value rather than in a state machine. */
static volatile uint16_t tx_frame;
static volatile uint8_t  tx_left;

/* ------------------------------------------------------------- receiver */
/*
 * rx_left counts samples still to take, and is the receiver's whole state:
 *
 *   0      idle, and the pin-change interrupt is armed
 *   10     the next sample is the middle of the start bit
 *   9..2   the eight data bits
 *   1      the middle of the stop bit
 *
 * Sampling the stop bit rather than re-arming after the last data bit is what
 * makes a framing error detectable. A receiver that re-armed early would take
 * the 0->1 data-to-stop transition for line activity and could resynchronise on
 * the middle of a frame, turning one bad byte into a run of them.
 */
static volatile uint8_t rx_left;
static volatile uint8_t rx_shift;

static void rx_arm(void)
{
    rx_left = 0u;
    TIMSK   = (uint8_t)(TIMSK & (uint8_t)~_BV(OCIE1B));
    /* Clear a pin-change flag raised during the frame BEFORE re-enabling, or
     * the edges of the byte just received fire an interrupt the moment the mask
     * goes back on -- and the receiver starts a frame that is already over. */
    GIFR    = (uint8_t)_BV(PCIF);
    GIMSK  |= (uint8_t)_BV(PCIE);
}

ag_status_t ag_hal_uart_init(uint32_t baud)
{
    uint32_t ticks;

    /* The compile-time numbers above describe the board's declared baud. This
     * honours the ARGUMENT, because the contract says it takes one -- and
     * refuses a baud this prescaler cannot express rather than running at some
     * other rate and leaving a host to wonder. */
    if (baud == 0uL) {
        return AG_ERR_RANGE;
    }
    ticks = (UART_TICK_HZ + (baud / 2uL)) / baud;
    if (ticks < 16uL || ticks > 256uL) {
        return AG_ERR_RANGE;
    }

    /* TX idles HIGH. Setting the level before the direction matters: the other
     * order drives the line low for the instant between the two writes, which a
     * listening host reads as a start bit and then a framing error. */
    PORTB |= TX_MASK;
    DDRB  |= TX_MASK;

    /* RX is an input WITH ITS PULL-UP ON. A floating input on a part with no
     * transmitter attached picks up enough noise to trigger the pin-change
     * interrupt continuously, and the device spends its life starting frames
     * that never finish. */
    DDRB   = (uint8_t)(DDRB & (uint8_t)~RX_MASK);
    PORTB |= RX_MASK;

    /* CTC1 makes OCR1C the TOP the counter clears on, which is what turns
     * Timer1 into a bit clock rather than an 8-bit free-run. The prescaler bits
     * go in the same write, so the timer starts already configured. */
    TCNT1 = 0u;
    OCR1C = (uint8_t)(ticks - 1uL);
    OCR1A = 0u;
    OCR1B = 0u;
    TCCR1 = (uint8_t)(_BV(CTC1) | (AG_UART_SOFT_CS & 0x0Fu));

    rx_head = rx_tail = tx_head = tx_tail = 0u;
    tx_frame = 0u;
    tx_left  = 0u;
    rx_shift = 0u;

    /* Neither compare interrupt is enabled yet: the transmitter turns its own
     * on when it has something to send, and the receiver's is turned on by the
     * start edge. An always-running bit-clock interrupt on an idle link would
     * cost 19200 interrupts a second for nothing. */
    PCMSK |= RX_MASK;
    rx_arm();
    return AG_OK;
}

/*
 * The transmitter. Emits one bit per period and stops its own interrupt when
 * the ring runs dry, so an idle link costs nothing.
 */
ISR(TIMER1_COMPA_vect)
{
    if (tx_left == 0u) {
        if (tx_tail == tx_head) {
            TIMSK = (uint8_t)(TIMSK & (uint8_t)~_BV(OCIE1A));
            return;
        }
        /* start bit 0 in bit 0, data in bits 1..8, stop bit 1 in bit 9 */
        tx_frame = (uint16_t)(((uint16_t)tx_buf[tx_tail] << 1) | 0x0200u);
        tx_tail  = (uint8_t)((tx_tail + 1u) & (AG_UART_TX_RING - 1u));
        tx_left  = 10u;
    }
    if (tx_frame & 1u) {
        PORTB |= TX_MASK;
    } else {
        PORTB = (uint8_t)(PORTB & (uint8_t)~TX_MASK);
    }
    tx_frame = (uint16_t)(tx_frame >> 1);
    tx_left--;
}

/*
 * The start edge.
 *
 * A pin change is either edge, and only the falling one begins a frame -- the
 * rising one is the end of a stop bit or of a pull-up settling. Checking the
 * level rather than trusting the interrupt is the difference between a receiver
 * that starts a frame per byte and one that starts two.
 */
ISR(PCINT0_vect)
{
    if ((PINB & RX_MASK) != 0u) {
        return;                      /* rising edge: not a start bit */
    }

    /*
     * The sampling phase, set once per frame. OCR1B lands half a bit from now,
     * and because the timer's period IS one bit, every compare after it is one
     * bit later with no further arithmetic.
     *
     * WIDENED TO 16 BITS, and that is the whole correctness of this block. The
     * counter runs 0..OCR1C, which is 0..207 here -- NOT 0..255 -- so the sum
     * has to wrap at OCR1C+1 and not at the byte. Done in eight bits, a start
     * edge arriving late in the period wraps at 256 and the sample lands at a
     * phase off by the difference: 48 ticks into the bit instead of 96. The
     * link then works for most bytes and fails for the ones that arrive in the
     * last quarter of a period, which is the kind of fault that gets blamed on
     * the cable.
     *
     * The half-bit is derived from OCR1C rather than from the compile-time
     * baud, so it is whatever the timer was actually programmed with -- a
     * caller that passed a different baud to ag_hal_uart_init cannot end up
     * sampling on the phase of a baud it did not ask for.
     */
    {
        uint16_t top = (uint16_t)OCR1C + 1u;
        uint16_t at  = (uint16_t)((uint16_t)TCNT1 + (uint16_t)(top >> 1));
        if (at >= top) {
            at = (uint16_t)(at - top);
        }
        OCR1B = (uint8_t)at;
    }

    rx_left  = 10u;
    rx_shift = 0u;
    /* A compare flag raised while the mask was off would fire the sampling ISR
     * immediately and at the wrong phase. */
    TIFR     = (uint8_t)_BV(OCF1B);
    TIMSK   |= (uint8_t)_BV(OCIE1B);
    /* No more edges until this frame is done. The data bits are edges too, and
     * every one of them would otherwise re-arm the phase mid-byte. */
    GIMSK    = (uint8_t)(GIMSK & (uint8_t)~_BV(PCIE));
}

/* One sample, in the middle of one bit. */
ISR(TIMER1_COMPB_vect)
{
    uint8_t high = (uint8_t)((PINB & RX_MASK) != 0u);

    if (rx_left == 10u) {
        /* The middle of what claimed to be a start bit. If the line has already
         * gone back high it was a glitch, not a frame, and treating it as one
         * would shift eight bits of noise into the ring. */
        if (high) {
            rx_arm();
        } else {
            rx_left = 9u;
        }
        return;
    }

    if (rx_left == 1u) {
        /* The middle of the stop bit. A LOW here is a framing error -- the byte
         * is dropped rather than delivered, because a frame whose boundaries
         * were wrong has bits from two of them. The framer above resynchronises
         * on a CRC failure, which is a recoverable frame loss; a byte that is
         * wrong but well-formed is not. */
        if (high) {
            uint8_t next = (uint8_t)((rx_head + 1u) & (AG_UART_RX_RING - 1u));
            /* A full ring drops the byte rather than overwriting the oldest,
             * for the reason the hardware UART gives: a corrupt frame that
             * still passes a length check is worse than a lost one. */
            if (next != rx_tail) {
                rx_buf[rx_head] = rx_shift;
                rx_head         = next;
            }
        }
        rx_arm();
        return;
    }

    /* A data bit. Serial is LSB first, so each arrival goes in at the top and
     * the byte shifts down under it. */
    rx_shift = (uint8_t)(rx_shift >> 1);
    if (high) {
        rx_shift |= 0x80u;
    }
    rx_left--;
}

uint8_t ag_hal_uart_rx(uint8_t *out)
{
    if (rx_tail == rx_head) {
        return 0u;
    }
    *out    = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & (AG_UART_RX_RING - 1u));
    return 1u;
}

uint8_t ag_hal_uart_tx_try(uint8_t byte)
{
    uint8_t next = (uint8_t)((tx_head + 1u) & (AG_UART_TX_RING - 1u));

    if (next == tx_tail) {
        return 0u;                      /* full: the caller retries next poll */
    }
    tx_buf[tx_head] = byte;
    tx_head         = next;
    /* TIMSK is read-modify-written by both ISRs here, so this one -- the only
     * one in thread context -- must be atomic. Without the guard an interrupt
     * landing between the read and the write loses whichever bit the ISR
     * changed, and the symptom is a transmitter or a receiver that stops for
     * good under load. */
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        /*
         * CLEAR THE STALE COMPARE FLAG BEFORE UNMASKING, or the first bit of
         * the next reply is short.
         *
         * Timer1 free-runs -- it is also the receiver's bit clock -- so OCF1A
         * keeps being set while OCIE1A is masked and an idle transmitter
         * accumulates one. Unmasking then takes the interrupt IMMEDIATELY, at
         * whatever phase the timer happens to be at, and the next compare
         * arrives less than a bit later: the start bit is truncated and the
         * byte is framed wrong.
         *
         * OCF1A is write-one-to-clear. It must be cleared inside this atomic
         * block and before the unmask, because a compare landing between the
         * two would set it again and the same window reopens.
         *
         * The failure only appears on the SECOND reply after an idle period,
         * which is why no test here catches it and why the bit timing has
         * never been executed on emulated silicon.
         */
        TIFR   = (uint8_t)_BV(OCF1A);
        TIMSK |= (uint8_t)_BV(OCIE1A);
    }
    return 1u;
}

/*
 * Empty ring AND nothing still shifting out. The deferred reset waits on this,
 * and on a software UART the distinction is larger than on a hardware one: a
 * frame takes 521 us to leave here at 19200, so a reset that only checked the
 * ring would truncate the acknowledgement the host is waiting for by most of a
 * character.
 */
uint8_t ag_hal_uart_tx_idle(void)
{
    return (uint8_t)(tx_head == tx_tail && tx_left == 0u);
}
