/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATtiny85 pin table, checked against the datasheet on x86.
 *
 * Two things here are worth more than the rest put together.
 *
 * THE ADC CHANNELS DO NOT RUN WITH THE PORT BITS. ADC0 is on PB5, ADC1 on PB2,
 * ADC2 on PB4 and ADC3 on PB3 -- a mapping with no arithmetic relation to the
 * pin number at all, and one that a table written from memory gets wrong in a
 * way that works perfectly and reads the wrong pin. That is the class of defect
 * this suite exists for, and this part is the worst instance of it in the tree.
 *
 * THE PORT ADDRESS IS 0x36 AND NOT 0x23. This part's ports live in the LOW I/O
 * space, and the descriptor stores the memory-mapped alias -- so the number is
 * the 328P's plus nothing, it is a different number entirely. A paste from
 * another board here would write DDRB into a reserved register and every pin
 * would read back as whatever the bus last carried.
 *
 * This suite runs under `make board-sweep`, which compiles it against this
 * board's own profile. It is not in `make test`, which is configured for one
 * board and would compile this against the 328P's header.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void expect_pin(tfw_pin_t pin, uint16_t port, uint8_t bit)
{
    tfw_pinref_t r;
    char        msg[48];

    snprintf(msg, sizeof msg, "pin %u", (unsigned)pin);
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_board_pin(pin, &r), msg);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(port, r.port, msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(bit, r.bit, msg);
}

/* Six pins, one port, pin N on bit N. The only board in the tree where that
 * sentence is the whole map -- which is worth asserting rather than assuming,
 * because it is also the shape a careless table accidentally has. */
static void test_every_pin_is_port_b_at_its_own_bit(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        expect_pin(i, TFW_ADDR_PINB, i);
    }
}

/* 0x36, not 0x23. The low I/O space plus the 0x20 memory-map offset. */
static void test_the_port_address_is_the_low_io_alias(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x36u, TFW_ADDR_PINB);
}

/*
 * THE ADC MAP, spelled out one pin at a time because there is no rule to check.
 *
 * ADC0 PB5, ADC1 PB2, ADC2 PB4, ADC3 PB3. A table that ran the channels upward
 * with the port bits would give PB2 channel 0 and PB5 channel 3, and every
 * reading would come from a different pin than the host asked for.
 */
static void test_the_adc_channels_are_the_datasheets_and_not_the_pin_numbers(void)
{
    uint8_t ch;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(5u, &ch));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, ch, "ADC0 is on PB5");
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(2u, &ch));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1u, ch, "ADC1 is on PB2");
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(4u, &ch));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, ch, "ADC2 is on PB4");
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(3u, &ch));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3u, ch, "ADC3 is on PB3");
}

/* PB0 and PB1 are the software UART and have no ADC channel at all. The
 * negative half of the map: a table that gave them one would have to have taken
 * it from a pin that really has it. */
static void test_the_uart_pins_have_no_adc_channel(void)
{
    uint8_t ch;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(0u, &ch));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(1u, &ch));
}

/* TFW_ADC_COUNT is the HAL's range-check bound, so it is one past the highest
 * real channel. Above it ADMUX selects differential pairs, the bandgap and
 * ground -- none of which is a pin, and all of which would return a plausible
 * number for a request that should have been refused. */
static void test_the_adc_bound_is_one_past_the_highest_real_channel(void)
{
    TEST_ASSERT_EQUAL_UINT8(4u, TFW_ADC_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;
        if (tfw_board_adc_channel(i, &ch) == TFW_OK) {
            TEST_ASSERT_TRUE_MESSAGE(ch < TFW_ADC_COUNT, "a pin claims a channel the HAL will refuse");
        }
    }
}

/*
 * PINS 0 AND 1 ARE THE LINK. On this part that is stronger than advice: the
 * transmitter's levels are written by an ISR off Timer1, so a host driving pin
 * 1 does not merely talk over the line, it fights the bit clock.
 */
static void test_the_uart_pins_are_marked_in_use(void)
{
    TEST_ASSERT_TRUE(tfw_board_pin_flags(0u) & TFW_PIN_IN_USE);
    TEST_ASSERT_TRUE(tfw_board_pin_flags(1u) & TFW_PIN_IN_USE);
}

/*
 * PIN 5 IS /RESET. It is reserved, and it is deliberately NOT marked digital:
 * driving it resets the part, and it becomes an ordinary pin only when RSTDISBL
 * is programmed -- which also ends ISP programming, so it is not a decision
 * firmware can make on a host's behalf. Its ADC channel is still reported,
 * because the channel is real.
 */
static void test_the_reset_pin_is_reserved_and_not_advertised_as_digital(void)
{
    uint8_t f = tfw_board_pin_flags(5u);
    TEST_ASSERT_TRUE_MESSAGE(f & TFW_PIN_RESERVED, "/RESET must be reported reserved");
    TEST_ASSERT_FALSE_MESSAGE(f & TFW_PIN_DIGITAL, "/RESET must not be advertised as a digital pin");
    TEST_ASSERT_TRUE_MESSAGE(f & TFW_PIN_ADC, "ADC0 is real and belongs in the descriptor");
}

/*
 * NO PIN ON THIS BUILD CAN DO PWM, and the four that have a compare unit say
 * RESERVED rather than ABSENT.
 *
 * Timer0's OC0A and OC0B go to the millisecond tick; Timer1's OC1A and OC1B go
 * to the software UART's bit clock, which on a part with no USART is not
 * optional. State 2 tells a host the fix is a different build; state 0 would
 * tell it to move the load to another pin, and there is no other pin.
 */
static void test_no_pin_advertises_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        TEST_ASSERT_FALSE_MESSAGE(tfw_board_pin_flags(i) & TFW_PIN_PWM,
                                  "this build has no PWM: every timer is spoken for");
    }
}

static void test_the_pins_with_a_compare_unit_report_reserved_not_absent(void)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, tfw_board_pin_pwm_state(0u), "PB0 is OC0A, spent on the tick");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, tfw_board_pin_pwm_state(1u), "PB1 is OC0B/OC1A");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, tfw_board_pin_pwm_state(3u), "PB3 is /OC1B");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, tfw_board_pin_pwm_state(4u), "PB4 is OC1B");
}

static void test_the_pin_with_no_compare_unit_reports_none(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state(2u));   /* PB2 has none */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state(5u));   /* nor /RESET */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state(99u));  /* nor a pin that does not exist */
}

/* A PWM request must be refused for every pin, since the table it would index
 * is empty. Asserted rather than assumed, because an empty table plus an index
 * that was not refused is a read off the end of nothing. */
static void test_every_pwm_resolution_is_refused(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pwmref_t p;
        TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p),
                                  "a pin resolved to a PWM unit on a board with none");
    }
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;
    tfw_pwmref_t p;
    uint8_t     ch;
    uint8_t     page[TFW_PIN_PAGE_LEN];
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(255u, &r));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_adc_channel(TFW_PIN_COUNT, &ch));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pwm(TFW_PIN_COUNT, &p));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_flags(TFW_PIN_COUNT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin_page(TFW_PIN_COUNT, page));
}

/* No two pins may resolve to the same port and bit. A copy-paste slip in the
 * table is otherwise silent: both pins work, and they work as each other. */
static void test_no_two_pins_share_a_port_and_bit(void)
{
    for (uint8_t a = 0u; a < TFW_PIN_COUNT; a++) {
        tfw_pinref_t ra;
        if (tfw_board_pin(a, &ra) != TFW_OK) { continue; }
        for (uint8_t b = (uint8_t)(a + 1u); b < TFW_PIN_COUNT; b++) {
            tfw_pinref_t rb;
            char        msg[64];
            if (tfw_board_pin(b, &rb) != TFW_OK) { continue; }
            snprintf(msg, sizeof msg, "pins %u and %u collide", (unsigned)a, (unsigned)b);
            TEST_ASSERT_FALSE_MESSAGE(ra.port == rb.port && ra.bit == rb.bit, msg);
        }
    }
}

/* Likewise no two pins may claim the same ADC channel. */
static void test_no_two_pins_share_an_adc_channel(void)
{
    for (uint8_t a = 0u; a < TFW_PIN_COUNT; a++) {
        uint8_t ca;
        if (tfw_board_adc_channel(a, &ca) != TFW_OK) { continue; }
        for (uint8_t b = (uint8_t)(a + 1u); b < TFW_PIN_COUNT; b++) {
            uint8_t cb;
            char    msg[64];
            if (tfw_board_adc_channel(b, &cb) != TFW_OK) { continue; }
            snprintf(msg, sizeof msg, "pins %u and %u share ADC channel", (unsigned)a, (unsigned)b);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(ca, cb, msg);
        }
    }
}

static void test_every_pin_is_either_digital_or_adc(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t f = tfw_board_pin_flags(i);
        TEST_ASSERT_TRUE_MESSAGE(f & (TFW_PIN_DIGITAL | TFW_PIN_ADC), "a pin that can do nothing");
    }
}

/* The port and bit CAPS reports in the pin page: one port, pin N on bit N. It
 * cannot be derived from tfw_board_pin(), whose tfw_pinref_t carries the PINx
 * memory address rather than an index. Asserted through the page and not a
 * packed byte, because the packing is the architecture's (hal_types.h). */
static void test_the_page_carries_the_descriptors_port_and_bit(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t page[TFW_PIN_PAGE_LEN];

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(i, page));
        TEST_ASSERT_EQUAL_HEX8(0u, page[2]);
        TEST_ASSERT_EQUAL_HEX8(i, page[3]);
    }
}

/* This part has one port, and the descriptor must say so -- a count of two
 * would leave a second row zero-filled, and a pin packed with port index 1
 * would resolve to PINx address 0x0000. */
static void test_the_board_has_exactly_one_port_and_six_pins(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, TFW_PORT_COUNT);
    TEST_ASSERT_EQUAL_UINT8(6u, tfw_board_pin_count());
    TEST_ASSERT_EQUAL_STRING("attiny85", tfw_board_name());
}

/*
 * THE SIZING THIS PART DEVIATES ON, asserted so that a change to it is a
 * decision rather than an accident.
 *
 * These are not style: the payload drives both frame buffers and the
 * transport's copy, so raising it costs about three bytes of SRAM per byte on a
 * part that has 512. A future change that restores the tree's defaults here
 * would link, run, and overflow the stack under a full-length request.
 */
static void test_the_sizing_deviations_are_the_ones_the_ram_budget_assumes(void)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(32u, TFW_MAX_PAYLOAD, "the RAM budget assumes a 32-byte payload");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4u, TFW_SCHED_SLOTS, "four slots: six pins, two of them the link");
    TEST_ASSERT_EQUAL_UINT8(16u, TFW_UART_RX_RING);
    TEST_ASSERT_EQUAL_UINT8(16u, TFW_UART_TX_RING);
}

/*
 * WHAT CAPS WILL TELL A HOST, asserted at the source.
 *
 * The feature bitmap is derived from the gates rather than hand-maintained, so
 * this checks the GATES -- and it is the test that fails if someone turns one of
 * them back on without the silicon to support it. Every bit below is off
 * because the part cannot do the thing, except 1-Wire, which is off because it
 * shares a translation unit with two drivers that do not compile here.
 */
static void test_caps_reports_absent_everything_this_part_cannot_do(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_PWM,     "no timer is free for PWM on this part");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_TONE,    "no Timer2 on this part");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_I2C,     "a USI is not a TWI");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_SPI,     "a USI is not an SPI peripheral");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_ONEWIRE, "1-Wire shares hal_bus.c with TWI and SPI");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_ATTEST,  "no boot section means no key store");
    TEST_ASSERT_EQUAL_MESSAGE(0, TFW_FEATURE_ATTEST_CSR, "no element tier to sign it");
    /* and the primitives that DO survive */
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_GPIO,   "gpio is the point of the board");
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_ANALOG, "three ADC channels are real");
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_SCHED,  "timed levels need no peripheral");
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_PULSE,  "edge capture is a GPIO and a busy loop");
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_EEPROM, "512 bytes of it");
}

/*
 * THE SOFTWARE UART'S DECLARED BAUD AND THE BIT-TIME ERROR IT PRODUCES.
 *
 * Recomputed here from the same three numbers the firmware derives its timer
 * from -- F_CPU, the prescaler and the baud -- so the figure quoted in
 * docs/boards/attiny85.md is checked rather than remembered. The HAL asserts the
 * same bound at compile time; this states the actual value, so a change to any
 * of the three shows up as a number that moved.
 *
 * Timer1 at CK/2 is 4 MHz. A bit wants 208.333 ticks and gets 208, so the bit
 * time is 52.000 us against 52.0833 nominal: -0.16%.
 */
static void test_the_soft_uarts_bit_time_error_is_the_documented_one(void)
{
    const uint32_t baud    = (uint32_t)TFW_UART_BAUD;
    const uint32_t tick_hz = 8000000u / (1u << (TFW_UART_SOFT_CS - 1u));
    uint32_t       ticks;
    uint32_t       ideal_milli;
    uint32_t       err_ppm;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(19200u, baud,
                                     "115200 is 69 CPU cycles a bit at 8 MHz and does not fit an ISR");
    TEST_ASSERT_EQUAL_UINT32(4000000u, tick_hz);

    ticks = (tick_hz + (baud / 2u)) / baud;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(208u, ticks, "OCR1C would be this minus one");
    TEST_ASSERT_TRUE_MESSAGE(ticks <= 256u, "more ticks a bit than OCR1C can express");

    /* Ticks a bit x 1000, so the fractional part survives integer division.
     * Widened first: 4 MHz x 1000 is within a hair of a uint32_t's ceiling, and
     * a board with a faster clock would wrap it and report a perfect divisor. */
    ideal_milli = (uint32_t)(((uint64_t)tick_hz * 1000u) / baud);
    TEST_ASSERT_EQUAL_UINT32(208333u, ideal_milli);
    err_ppm = (uint32_t)(((uint64_t)(ideal_milli - ticks * 1000u) * 1000000u) / ideal_milli);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1598u, err_ppm, "the documented bit-time error is -0.160%");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_pin_is_port_b_at_its_own_bit);
    RUN_TEST(test_the_port_address_is_the_low_io_alias);
    RUN_TEST(test_the_adc_channels_are_the_datasheets_and_not_the_pin_numbers);
    RUN_TEST(test_the_uart_pins_have_no_adc_channel);
    RUN_TEST(test_the_adc_bound_is_one_past_the_highest_real_channel);
    RUN_TEST(test_the_uart_pins_are_marked_in_use);
    RUN_TEST(test_the_reset_pin_is_reserved_and_not_advertised_as_digital);
    RUN_TEST(test_no_pin_advertises_pwm);
    RUN_TEST(test_the_pins_with_a_compare_unit_report_reserved_not_absent);
    RUN_TEST(test_the_pin_with_no_compare_unit_reports_none);
    RUN_TEST(test_every_pwm_resolution_is_refused);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_no_two_pins_share_a_port_and_bit);
    RUN_TEST(test_no_two_pins_share_an_adc_channel);
    RUN_TEST(test_every_pin_is_either_digital_or_adc);
    RUN_TEST(test_the_page_carries_the_descriptors_port_and_bit);
    RUN_TEST(test_the_board_has_exactly_one_port_and_six_pins);
    RUN_TEST(test_the_sizing_deviations_are_the_ones_the_ram_budget_assumes);
    RUN_TEST(test_caps_reports_absent_everything_this_part_cannot_do);
    RUN_TEST(test_the_soft_uarts_bit_time_error_is_the_documented_one);
    return UNITY_END();
}
