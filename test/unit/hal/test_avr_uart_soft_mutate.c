/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_uart_soft.c, register-level (#163), on the ATtiny85 profile.
 *
 * Every register is a byte in regs[] behind the <avr/io.h> stand-in, and each
 * interrupt is an ordinary function the <avr/interrupt.h> stand-in names for
 * its vector, so a frame is driven bit by bit: TIMER1_COMPA_vect is one
 * transmit bit time, PCINT0_vect the start edge, TIMER1_COMPB_vect one receive
 * sample with PINB set to the level on the line. ATOMIC_BLOCK is the
 * <util/atomic.h> stand-in, so the suite sees which TIMSK and TIFR accesses
 * tfw_hal_uart_tx_try makes inside it.
 *
 * The board's numbers: F_CPU 8 MHz, CK/2, so a 4 MHz tick and 208 ticks a bit
 * at 19200 (OCR1C 207). RX is PB0, TX is PB1, and both rings are 16 bytes.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "tfw/config.h"
#include "avr/io.h"

void TIMER1_COMPA_vect(void);
void PCINT0_vect(void);
void TIMER1_COMPB_vect(void);

#define RING    16u
#define RX_BIT  0x01u
#define TX_BIT  0x02u

static uint8_t regs[TFW_AVR_REGS];
static int     atomic_depth, atomic_blocks;
static int     in_tx, outside_atomic;     /* TIMSK/TIFR touched by tx_try unguarded */
static int     tifr_then_timsk;            /* order of tx_try's first two accesses */
static int     tx_access_seq;
static int     in_init, tx_glitch;         /* TX driven as an output while low */

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE(id < TFW_AVR_REGS);
    if (in_init && (regs[TFW_AVR_DDRB] & TX_BIT) != 0u && (regs[TFW_AVR_PORTB] & TX_BIT) == 0u) {
        tx_glitch++;
    }
    if (in_tx && (id == TFW_AVR_TIMSK || id == TFW_AVR_TIFR)) {
        if (atomic_depth == 0) {
            outside_atomic++;
        }
        if (tx_access_seq++ == 0) {
            tifr_then_timsk = id == TFW_AVR_TIFR;
        }
    }
    return &regs[id];
}

int tfw_avr_atomic_enter(void)
{
    atomic_depth++;
    atomic_blocks++;
    return 1;
}

int tfw_avr_atomic_leave(void)
{
    atomic_depth--;
    return 0;
}

static void init_ok(uint32_t baud)
{
    in_init = 1;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_uart_init(baud));
    in_init = 0;
}

static uint8_t tx(uint8_t b)
{
    uint8_t r;

    in_tx         = 1;
    tx_access_seq = 0;
    r             = tfw_hal_uart_tx_try(b);
    in_tx         = 0;
    return r;
}

static uint8_t line(void)
{
    return (uint8_t)((regs[TFW_AVR_PORTB] & TX_BIT) != 0u);
}

/* Ten bit times; the byte framed on PB1, or -1 if the start or stop bit is wrong. */
static int tx_frame_out(void)
{
    unsigned i;
    int      b = 0;

    TIMER1_COMPA_vect();
    if (line() != 0u) {
        return -1;
    }
    for (i = 0u; i < 8u; i++) {
        TIMER1_COMPA_vect();
        b |= (int)line() << i;
    }
    TIMER1_COMPA_vect();
    return line() == 1u ? b : -1;
}

/* A start edge, then ten mid-bit samples: start, eight data LSB first, stop. */
static void rx_frame(uint8_t b, int stop_high)
{
    unsigned i;

    regs[TFW_AVR_PINB] = 0u;
    PCINT0_vect();
    TIMER1_COMPB_vect();                        /* middle of the start bit */
    for (i = 0u; i < 8u; i++) {
        regs[TFW_AVR_PINB] = (uint8_t)((b >> i) & 1u);
        TIMER1_COMPB_vect();
    }
    regs[TFW_AVR_PINB] = stop_high ? RX_BIT : 0u;
    TIMER1_COMPB_vect();
    regs[TFW_AVR_PINB] = RX_BIT;                /* idle */
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    atomic_depth = atomic_blocks = in_tx = outside_atomic = 0;
    tifr_then_timsk = tx_access_seq = in_init = tx_glitch = 0;
    init_ok(TFW_UART_BAUD);
    regs[TFW_AVR_PINB] = RX_BIT;
}

void tearDown(void) {}

static void test_init_refuses_a_baud_the_prescaler_cannot_express(void)
{
    static const uint32_t bad[] = { 0u, 270000u, 15564u, 1000u, 4000000u };
    unsigned              i, r;

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        memset(regs, 0, sizeof regs);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_uart_init(bad[i]));
        for (r = 0u; r < TFW_AVR_REGS; r++) {
            TEST_ASSERT_EQUAL_HEX8(0u, regs[r]);            /* nothing touched */
        }
    }
}

static void test_init_bit_period_rounds_to_nearest(void)
{
    static const struct { uint32_t baud; uint8_t ocr1c; } v[] = {
        { 19200u, 207u },   /* 208.3 ticks */
        { 19000u, 210u },   /* 210.5 rounds up to 211; truncation would give 210 */
        { 250000u, 15u },   /* 16 ticks: the floor */
        { 15600u, 255u },   /* 256.4 ticks: the ceiling */
    };
    unsigned i;

    for (i = 0u; i < sizeof v / sizeof v[0]; i++) {
        regs[TFW_AVR_OCR1C] = 0xEEu;
        init_ok(v[i].baud);
        TEST_ASSERT_EQUAL_UINT8(v[i].ocr1c, regs[TFW_AVR_OCR1C]);
    }
    /* Each side of the edges: 15.3 and 256.5 ticks, and 9600 at 416.7. */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_uart_init(260000u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_uart_init(15565u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_uart_init(9600u));
}

static void test_init_programs_pins_timer_and_receiver(void)
{
    memset(regs, 0, sizeof regs);
    regs[TFW_AVR_DDRB]  = 0xF1u;    /* RX was an output: init makes it an input */
    regs[TFW_AVR_PORTB] = 0x80u;
    regs[TFW_AVR_TCNT1] = 0x77u;
    regs[TFW_AVR_OCR1A] = 0x66u;
    regs[TFW_AVR_OCR1B] = 0x55u;
    regs[TFW_AVR_TIMSK] = 0x21u;    /* OCIE1B left on, and an unrelated bit */
    regs[TFW_AVR_GIMSK] = 0x01u;
    regs[TFW_AVR_PCMSK] = 0x10u;
    init_ok(TFW_UART_BAUD);

    TEST_ASSERT_EQUAL_INT(0, tx_glitch);                    /* level before direction */
    TEST_ASSERT_EQUAL_HEX8(0x83u, regs[TFW_AVR_PORTB]);     /* TX high, RX pull-up */
    TEST_ASSERT_EQUAL_HEX8(0xF2u, regs[TFW_AVR_DDRB]);      /* TX out, RX in */
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TCNT1]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_OCR1A]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_OCR1B]);
    TEST_ASSERT_EQUAL_HEX8(207u, regs[TFW_AVR_OCR1C]);
    TEST_ASSERT_EQUAL_HEX8(0x82u, regs[TFW_AVR_TCCR1]);     /* CTC1 | CK/2 */
    TEST_ASSERT_EQUAL_HEX8(0x01u, regs[TFW_AVR_TIMSK]);     /* no compare interrupt */
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIFR]);      /* PCIF cleared */
    TEST_ASSERT_EQUAL_HEX8(0x21u, regs[TFW_AVR_GIMSK]);     /* PCIE on */
    TEST_ASSERT_EQUAL_HEX8(0x11u, regs[TFW_AVR_PCMSK]);     /* PCINT0 on */
}

static void test_init_empties_rings_and_abandons_a_frame(void)
{
    uint8_t b = 0xEEu;

    rx_frame(0x31u, 1);
    TEST_ASSERT_EQUAL_UINT8(1u, tx(0x41u));
    TEST_ASSERT_EQUAL_UINT8(1u, tx(0x42u));
    TIMER1_COMPA_vect();                                    /* 0x41 under way */
    init_ok(TFW_UART_BAUD);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TIMER1_COMPA_vect();                                    /* nothing to send */
    TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_TIMSK] & 0x40u);
}

static void test_transmit_frames_lsb_first_and_stops_its_clock(void)
{
    regs[TFW_AVR_PORTB] = (uint8_t)(regs[TFW_AVR_PORTB] | 0x30u);
    regs[TFW_AVR_TIFR]  = 0x20u;    /* OCF1B pending: the receiver's, not to be cleared */
    regs[TFW_AVR_TIMSK] = 0x08u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_UINT8(1u, tx(0xA5u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());   /* ring holds it */
    TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
    TEST_ASSERT_EQUAL_INT(0, outside_atomic);
    TEST_ASSERT_EQUAL_INT(0, atomic_depth);
    TEST_ASSERT_EQUAL_INT(1, tifr_then_timsk);             /* flag cleared, then unmasked */
    TEST_ASSERT_EQUAL_HEX8(0x40u, regs[TFW_AVR_TIFR]);     /* a write of OCF1A only, not |= */
    TEST_ASSERT_EQUAL_HEX8(0x48u, regs[TFW_AVR_TIMSK]);    /* OCIE1A, the rest kept */

    TIMER1_COMPA_vect();
    TEST_ASSERT_EQUAL_UINT8(0u, line());                   /* start bit */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());   /* still shifting */
    {
        unsigned i;
        for (i = 0u; i < 8u; i++) {
            TIMER1_COMPA_vect();
            TEST_ASSERT_EQUAL_UINT8((0xA5u >> i) & 1u, line());
            TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_tx_idle());
        }
    }
    TIMER1_COMPA_vect();
    TEST_ASSERT_EQUAL_UINT8(1u, line());                   /* stop bit */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    TEST_ASSERT_EQUAL_HEX8(0x31u, regs[TFW_AVR_PORTB] & 0xFDu);   /* other pins kept */

    TIMER1_COMPA_vect();                                   /* dry: clock off */
    TEST_ASSERT_EQUAL_HEX8(0x08u, regs[TFW_AVR_TIMSK]);
    TEST_ASSERT_EQUAL_UINT8(1u, line());
}

static void test_transmit_back_to_back_frames(void)
{
    static const uint8_t m[] = { 0x00u, 0xFFu, 0x01u, 0x80u, 0x5Au };
    unsigned             i;

    for (i = 0u; i < sizeof m; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tx(m[i]));
    }
    for (i = 0u; i < sizeof m; i++) {
        TEST_ASSERT_EQUAL_INT(m[i], tx_frame_out());
    }
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
}

static void test_transmit_ring_holds_ring_minus_one(void)
{
    unsigned i, lap;

    for (lap = 0u; lap < 3u; lap++) {
        for (i = 0u; i < RING - 1u; i++) {
            TEST_ASSERT_EQUAL_UINT8(1u, tx((uint8_t)(lap * 40u + i)));
        }
        atomic_blocks       = 0;
        regs[TFW_AVR_TIMSK] = 0u;
        TEST_ASSERT_EQUAL_UINT8(0u, tx(0xAAu));            /* full */
        TEST_ASSERT_EQUAL_INT(0, atomic_blocks);           /* refused: not armed */
        TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_TIMSK]);
        for (i = 0u; i < RING - 1u; i++) {
            TEST_ASSERT_EQUAL_INT((int)(lap * 40u + i), tx_frame_out());
        }
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_tx_idle());
    }
}

static void test_start_edge_sets_the_sampling_phase(void)
{
    static const struct { uint32_t baud; uint8_t tcnt, at; } v[] = {
        { 19200u, 10u, 114u },     /* 10 + 104 */
        { 19200u, 103u, 207u },    /* the last tick of the period */
        { 19200u, 104u, 0u },      /* exactly one period: wraps to 0 */
        { 19200u, 150u, 46u },     /* wraps at 208, not at 256 */
        { 19200u, 200u, 96u },     /* past 255: 8 bits would give 48 */
        { 15600u, 200u, 72u },     /* top 256: 328 - 256 */
        { 250000u, 15u, 7u },      /* top 16: 23 - 16 */
    };
    unsigned i;

    for (i = 0u; i < sizeof v / sizeof v[0]; i++) {
        init_ok(v[i].baud);
        regs[TFW_AVR_TCNT1] = v[i].tcnt;
        regs[TFW_AVR_TIFR]  = 0u;
        regs[TFW_AVR_TIMSK] = 0x40u;
        regs[TFW_AVR_PINB]  = 0u;
        PCINT0_vect();
        TEST_ASSERT_EQUAL_UINT8(v[i].at, regs[TFW_AVR_OCR1B]);
        TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_TIFR]);     /* OCF1B cleared */
        TEST_ASSERT_EQUAL_HEX8(0x60u, regs[TFW_AVR_TIMSK]);    /* OCIE1B on, OCIE1A kept */
        TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_GIMSK]);    /* edges off */
    }
}

static void test_rising_edge_is_not_a_start_bit(void)
{
    regs[TFW_AVR_PINB]  = (uint8_t)(RX_BIT | 0x0Cu);
    regs[TFW_AVR_OCR1B] = 0x33u;
    regs[TFW_AVR_TIFR]  = 0u;
    PCINT0_vect();
    TEST_ASSERT_EQUAL_HEX8(0x33u, regs[TFW_AVR_OCR1B]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TIFR]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TIMSK]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIMSK]);
    /* The other pins high do not hide this line going low. */
    regs[TFW_AVR_PINB] = 0xFEu;
    PCINT0_vect();
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_TIMSK]);
}

static void test_receive_bytes_in_order_and_rearm(void)
{
    static const uint8_t m[] = { 0xA5u, 0x00u, 0xFFu, 0x01u, 0x80u, 0x3Cu };
    unsigned             i;
    uint8_t              b = 0xEEu;

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, b);                       /* empty: *out alone */
    for (i = 0u; i < sizeof m; i++) {
        regs[TFW_AVR_GIFR] = 0u;
        rx_frame(m[i], 1);
        TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TIMSK]);  /* sampling off */
        TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIFR]);   /* stale PCIF cleared */
        TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIMSK]);  /* edges back on */
    }
    for (i = 0u; i < sizeof m; i++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
        TEST_ASSERT_EQUAL_HEX8(m[i], b);
    }
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
}

static void test_receive_drops_a_glitch_and_a_framing_error(void)
{
    uint8_t b = 0u;

    regs[TFW_AVR_PINB] = 0u;
    PCINT0_vect();
    regs[TFW_AVR_PINB] = RX_BIT;                            /* back high by mid-bit */
    regs[TFW_AVR_GIFR] = 0u;
    TIMER1_COMPB_vect();
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TIMSK]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIFR]);
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIMSK]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));

    rx_frame(0x77u, 0);                                     /* stop bit low */
    TEST_ASSERT_EQUAL_HEX8(0x20u, regs[TFW_AVR_GIMSK]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));

    rx_frame(0x12u, 1);                                     /* and the next is clean */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x12u, b);
}

/* The rest of PORTB high around a frame: only PB0 is the line. */
static void test_receive_samples_only_its_own_pin(void)
{
    unsigned i;
    uint8_t  b = 0u;

    regs[TFW_AVR_PINB] = 0xFEu;
    PCINT0_vect();
    TIMER1_COMPB_vect();                        /* start bit, PB0 low */
    for (i = 0u; i < 8u; i++) {
        regs[TFW_AVR_PINB] = (uint8_t)(0xFEu | ((0x4Bu >> i) & 1u));
        TIMER1_COMPB_vect();
    }
    regs[TFW_AVR_PINB] = 0xFFu;
    TIMER1_COMPB_vect();
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x4Bu, b);
}

static void test_receive_ring_keeps_ring_minus_one(void)
{
    unsigned i, lap;
    uint8_t  b;

    for (lap = 0u; lap < 3u; lap++) {
        for (i = 0u; i < RING + 2u; i++) {
            rx_frame((uint8_t)(lap * 50u + i), 1);
        }
        for (i = 0u; i < RING - 1u; i++) {
            TEST_ASSERT_EQUAL_UINT8(1u, tfw_hal_uart_rx(&b));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(lap * 50u + i), b);  /* newest dropped */
        }
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_hal_uart_rx(&b));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_refuses_a_baud_the_prescaler_cannot_express);
    RUN_TEST(test_init_bit_period_rounds_to_nearest);
    RUN_TEST(test_init_programs_pins_timer_and_receiver);
    RUN_TEST(test_init_empties_rings_and_abandons_a_frame);
    RUN_TEST(test_transmit_frames_lsb_first_and_stops_its_clock);
    RUN_TEST(test_transmit_back_to_back_frames);
    RUN_TEST(test_transmit_ring_holds_ring_minus_one);
    RUN_TEST(test_start_edge_sets_the_sampling_phase);
    RUN_TEST(test_rising_edge_is_not_a_start_bit);
    RUN_TEST(test_receive_bytes_in_order_and_rearm);
    RUN_TEST(test_receive_drops_a_glitch_and_a_framing_error);
    RUN_TEST(test_receive_samples_only_its_own_pin);
    RUN_TEST(test_receive_ring_keeps_ring_minus_one);
    return UNITY_END();
}
