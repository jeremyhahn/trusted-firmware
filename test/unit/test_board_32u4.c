/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATmega32U4 pin table, checked against the datasheet on x86.
 *
 * This is the payoff for putting the board descriptor above the HAL rather
 * than inside it. A wrong port bit for pin 13 is otherwise findable only by
 * wiring up a board and noticing the wrong LED -- and only if someone is
 * looking at that particular pin. Here it is a failing test on a laptop.
 *
 * The 32U4 earns this more than the 328P does. On a 328P the pin number and
 * the port bit are related by arithmetic, so one mistake looks wrong. Here
 * D0..D3 are PD2, PD3, PD1, PD0 and A0..A5 run DOWN port F from bit 7, so
 * every row is an independent fact and a transposition looks exactly as
 * plausible as the truth.
 *
 * The addresses are the real ones: PINB 0x23, PINC 0x26, PIND 0x29,
 * PINE 0x2C, PINF 0x2F.
 */

#include "unity.h"
#include "ag/board.h"
#include "ag/progmem.h"
#include "board_profile.h"

void setUp(void) {}
void tearDown(void) {}

static void expect_pin(ag_pin_t pin, uint16_t port, uint8_t bit)
{
    ag_pinref_t r;
    char        msg[48];

    snprintf(msg, sizeof msg, "pin %u", (unsigned)pin);
    TEST_ASSERT_EQUAL_MESSAGE(AG_OK, ag_board_pin(pin, &r), msg);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(port, r.port, msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(bit, r.bit, msg);
}

static void expect_adc(ag_pin_t pin, uint8_t channel)
{
    uint8_t ch;
    char    msg[48];

    snprintf(msg, sizeof msg, "pin %u adc", (unsigned)pin);
    TEST_ASSERT_EQUAL_MESSAGE(AG_OK, ag_board_adc_channel(pin, &ch), msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(channel, ch, msg);
}

/* D0..D3 are PD2, PD3, PD1, PD0 -- the low nibble of port D, in header order
 * rather than bit order, because the Leonardo routes RX/TX to USART1's pins and
 * SDA/SCL to the TWI pins and numbers them as they appear on the connector.
 * This is the single most transposable row in the table. */
static void test_digital_0_through_3_are_port_d_in_header_order(void)
{
    expect_pin(0u, AG_ADDR_PIND, 2u);   /* RXD1 */
    expect_pin(1u, AG_ADDR_PIND, 3u);   /* TXD1 */
    expect_pin(2u, AG_ADDR_PIND, 1u);   /* SDA  */
    expect_pin(3u, AG_ADDR_PIND, 0u);   /* SCL  */
}

/* The rest of port D: D4 is PD4, D6 is PD7 and D12 is PD6. Note that D6 is bit
 * 7 and D12 is bit 6 -- the two are easy to swap and both would still be
 * "somewhere on port D". */
static void test_the_remaining_port_d_pins(void)
{
    expect_pin(4u,  AG_ADDR_PIND, 4u);
    expect_pin(6u,  AG_ADDR_PIND, 7u);
    expect_pin(12u, AG_ADDR_PIND, 6u);
}

/* Port C contributes exactly two pins, at the TOP of the port: D5 is PC6 and
 * D13 is PC7. A 328P habit would look for PC0. */
static void test_port_c_carries_only_pins_5_and_13(void)
{
    expect_pin(5u,  AG_ADDR_PINC, 6u);
    expect_pin(13u, AG_ADDR_PINC, 7u);
}

/* Port E contributes exactly one pin: D7 is PE6. A port with one usable bit is
 * the kind of thing that gets dropped from a table entirely. */
static void test_port_e_carries_only_pin_7(void)
{
    expect_pin(7u, AG_ADDR_PINE, 6u);
}

/* D8..D11 are PB4..PB7, and D14..D17 are the SPI pins MISO, SCK, MOSI, SS --
 * PB3, PB1, PB2, PB0, which is NOT ascending order. */
static void test_port_b_carries_the_upper_digitals_and_the_spi_pins(void)
{
    expect_pin(8u,  AG_ADDR_PINB, 4u);
    expect_pin(9u,  AG_ADDR_PINB, 5u);
    expect_pin(10u, AG_ADDR_PINB, 6u);
    expect_pin(11u, AG_ADDR_PINB, 7u);
    expect_pin(14u, AG_ADDR_PINB, 3u);   /* MISO */
    expect_pin(15u, AG_ADDR_PINB, 1u);   /* SCK  */
    expect_pin(16u, AG_ADDR_PINB, 2u);   /* MOSI */
    expect_pin(17u, AG_ADDR_PINB, 0u);   /* SS   */
}

/* A0..A5 are pins 18..23 and run DOWN port F from bit 7, skipping bits 3 and 2
 * which the part does not bond out. A0 is PF7, not PF0. */
static void test_analog_0_through_5_run_down_port_f(void)
{
    expect_pin(18u, AG_ADDR_PINF, 7u);   /* A0 */
    expect_pin(19u, AG_ADDR_PINF, 6u);   /* A1 */
    expect_pin(20u, AG_ADDR_PINF, 5u);   /* A2 */
    expect_pin(21u, AG_ADDR_PINF, 4u);   /* A3 */
    expect_pin(22u, AG_ADDR_PINF, 1u);   /* A4 */
    expect_pin(23u, AG_ADDR_PINF, 0u);   /* A5 */
}

/* The built-in LED is pin 13, PORTC bit 7. Stated separately because it is the
 * pin every bring-up test blinks, and getting it wrong wastes an afternoon --
 * and because on a 328P it is PORTB bit 5, so the wrong answer is one somebody
 * already has memorised. */
static void test_the_builtin_led_is_pin_13_on_port_c_bit_7(void)
{
    expect_pin(13u, AG_ADDR_PINC, 7u);
    TEST_ASSERT_TRUE(ag_board_pin_flags(13u) & AG_PIN_LED);
}

/* A0..A5 carry ADC7 down to ADC0 -- the channel number falls as the pin number
 * rises, and the sequence skips ADC3 and ADC2 because the part does not have
 * them. Nothing about this can be derived; it is all datasheet. */
static void test_analog_pins_report_their_scattered_adc_channels(void)
{
    expect_adc(18u, 7u);
    expect_adc(19u, 6u);
    expect_adc(20u, 5u);
    expect_adc(21u, 4u);
    expect_adc(22u, 1u);
    expect_adc(23u, 0u);
}

/* Six digital pins double as analog inputs on channels 8..13 -- the bank behind
 * MUX5. The Arduino core calls these A6..A11 and gives them second pin numbers;
 * this descriptor puts the channel on the digital number instead, so pin 4 is
 * how a host reads ADC8. Getting the pin-to-channel pairing wrong here reads a
 * real voltage from the wrong wire, which is the failure that looks like a
 * sensor fault for a week. */
static void test_the_high_bank_channels_live_on_digital_pins(void)
{
    expect_adc(4u,  8u);
    expect_adc(12u, 9u);
    expect_adc(6u,  10u);
    expect_adc(8u,  11u);
    expect_adc(9u,  12u);
    expect_adc(10u, 13u);
}

/* Every channel the table claims must be inside the bound the HAL range-checks
 * against, or a pin exists that ag_hal_adc_read will refuse. Channel 13 is the
 * highest real one, so AG_ADC_COUNT has to be 14 -- not 12, the number of
 * channels that actually exist. */
static void test_every_claimed_channel_is_within_the_hal_bound(void)
{
    for (uint8_t i = 0u; i < AG_PIN_COUNT; i++) {
        uint8_t ch;
        char    msg[64];
        if (ag_board_adc_channel(i, &ch) != AG_OK) { continue; }
        snprintf(msg, sizeof msg, "pin %u channel %u exceeds AG_ADC_COUNT",
                 (unsigned)i, (unsigned)ch);
        TEST_ASSERT_TRUE_MESSAGE(ch < AG_ADC_COUNT, msg);
    }
}

/* ADC2 and ADC3 are not bonded out on any package of this part. A row claiming
 * either would convert a floating input and return plausible noise. */
static void test_no_pin_claims_a_channel_the_part_does_not_have(void)
{
    for (uint8_t i = 0u; i < AG_PIN_COUNT; i++) {
        uint8_t ch;
        char    msg[64];
        if (ag_board_adc_channel(i, &ch) != AG_OK) { continue; }
        snprintf(msg, sizeof msg, "pin %u claims nonexistent channel %u",
                 (unsigned)i, (unsigned)ch);
        TEST_ASSERT_TRUE_MESSAGE(ch != 2u && ch != 3u, msg);
    }
}

/* The purely digital pins must have no channel at all, or CAPS advertises an
 * analog read that cannot work. */
static void test_digital_only_pins_have_no_adc_channel(void)
{
    static const uint8_t digital_only[] = { 0u, 1u, 2u, 3u, 5u, 7u, 11u, 13u,
                                            14u, 15u, 16u, 17u };
    uint8_t ch;
    for (uint8_t i = 0u; i < sizeof digital_only / sizeof digital_only[0]; i++) {
        char msg[48];
        snprintf(msg, sizeof msg, "pin %u", (unsigned)digital_only[i]);
        TEST_ASSERT_EQUAL_MESSAGE(AG_ERR_UNSUPPORTED,
                                  ag_board_adc_channel(digital_only[i], &ch), msg);
    }
}

/* Timer0 is the millisecond tick, so pins 3 and 11 cannot do PWM even though
 * they have output-compare units -- OC0B on PD0 and OC0A on PB7. Reported as
 * reserved rather than absent: "no PWM here" and "this build spent it on the
 * clock" are different facts, and only one of them might change.
 *
 * Read from the table rather than through ag_board_pwm, because that lookup
 * collapses both cases to AG_ERR_UNSUPPORTED on purpose -- the distinction
 * exists only for CAPS, so only a direct read can prove it survived. */
static void test_the_tick_timers_pins_are_reserved_not_merely_absent(void)
{
    TEST_ASSERT_EQUAL_UINT8(AG_PWM_RSVD, ag_pgm_u8(&ag_board_pins[3].pwm_idx));
    TEST_ASSERT_EQUAL_UINT8(AG_PWM_RSVD, ag_pgm_u8(&ag_board_pins[11].pwm_idx));
    TEST_ASSERT_FALSE(ag_board_pin_flags(3u)  & AG_PIN_PWM);
    TEST_ASSERT_FALSE(ag_board_pin_flags(11u) & AG_PIN_PWM);
    /* while the three genuine PWM pins are advertised */
    TEST_ASSERT_TRUE(ag_board_pin_flags(5u)  & AG_PIN_PWM);
    TEST_ASSERT_TRUE(ag_board_pin_flags(9u)  & AG_PIN_PWM);
    TEST_ASSERT_TRUE(ag_board_pin_flags(10u) & AG_PIN_PWM);
}

/* Pins 6 and 13 have Timer4 compare outputs that this build does not drive, so
 * they must report PWM as genuinely absent -- not reserved. Reserved would tell
 * a host the capability is coming back, and it is not: it needs HAL support
 * that does not exist. */
static void test_the_timer4_pins_report_pwm_absent_not_reserved(void)
{
    TEST_ASSERT_EQUAL_UINT8(AG_PWM_NONE, ag_pgm_u8(&ag_board_pins[6].pwm_idx));
    TEST_ASSERT_EQUAL_UINT8(AG_PWM_NONE, ag_pgm_u8(&ag_board_pins[13].pwm_idx));
}

/* The three PWM pins must resolve to the registers that drive them. A pin whose
 * descriptor points at the other timer's OCR produces a duty cycle on a pin
 * nobody asked about, and silence on the one that was asked for. */
static void test_pwm_pins_resolve_to_their_own_compare_unit(void)
{
    ag_pwmref_t r;

    TEST_ASSERT_EQUAL(AG_OK, ag_board_pwm(9u, &r));
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_TCCR1A, r.tccra);
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_OCR1AL, r.ocr);
    TEST_ASSERT_EQUAL_UINT8(COM1A1_BIT, r.com_bit);
    TEST_ASSERT_TRUE(r.is16);

    TEST_ASSERT_EQUAL(AG_OK, ag_board_pwm(10u, &r));
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_TCCR1A, r.tccra);
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_OCR1BL, r.ocr);
    TEST_ASSERT_EQUAL_UINT8(COM1B1_BIT, r.com_bit);

    TEST_ASSERT_EQUAL(AG_OK, ag_board_pwm(5u, &r));
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_TCCR3A, r.tccra);
    TEST_ASSERT_EQUAL_HEX16(AG_ADDR_OCR3AL, r.ocr);
    TEST_ASSERT_EQUAL_UINT8(COM3A1_BIT, r.com_bit);
}

/* Pins 0 and 1 are the UART this firmware is talking over -- USART1 on PD2 and
 * PD3, the TTL pins, NOT the USB connector. A host that drove them would cut
 * the branch it is sitting on. */
static void test_the_uart_pins_are_marked_in_use(void)
{
    TEST_ASSERT_TRUE(ag_board_pin_flags(0u) & AG_PIN_IN_USE);
    TEST_ASSERT_TRUE(ag_board_pin_flags(1u) & AG_PIN_IN_USE);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    ag_pinref_t r;
    uint8_t     ch;
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, ag_board_pin(AG_PIN_COUNT, &r));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, ag_board_pin(255u, &r));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, ag_board_adc_channel(AG_PIN_COUNT, &ch));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_PIN, ag_board_pwm(AG_PIN_COUNT, (ag_pwmref_t *)0));
    TEST_ASSERT_EQUAL_UINT8(0u, ag_board_pin_flags(AG_PIN_COUNT));
}

/* No two pins may resolve to the same port and bit. A copy-paste slip in the
 * table is otherwise silent: both pins work, and they work as each other. This
 * is the invariant that forbids giving the core's A6..A11 numbers of their own,
 * since those are aliases for pins 4, 6, 8, 9, 10 and 12. */
static void test_no_two_pins_share_a_port_and_bit(void)
{
    for (uint8_t a = 0u; a < AG_PIN_COUNT; a++) {
        ag_pinref_t ra;
        if (ag_board_pin(a, &ra) != AG_OK) { continue; }
        for (uint8_t b = (uint8_t)(a + 1u); b < AG_PIN_COUNT; b++) {
            ag_pinref_t rb;
            char        msg[64];
            if (ag_board_pin(b, &rb) != AG_OK) { continue; }
            snprintf(msg, sizeof msg, "pins %u and %u collide", (unsigned)a, (unsigned)b);
            TEST_ASSERT_FALSE_MESSAGE(ra.port == rb.port && ra.bit == rb.bit, msg);
        }
    }
}

/* Likewise no two pins may claim the same ADC channel. */
static void test_no_two_pins_share_an_adc_channel(void)
{
    for (uint8_t a = 0u; a < AG_PIN_COUNT; a++) {
        uint8_t ca;
        if (ag_board_adc_channel(a, &ca) != AG_OK) { continue; }
        for (uint8_t b = (uint8_t)(a + 1u); b < AG_PIN_COUNT; b++) {
            uint8_t cb;
            char    msg[64];
            if (ag_board_adc_channel(b, &cb) != AG_OK) { continue; }
            snprintf(msg, sizeof msg, "pins %u and %u share ADC channel", (unsigned)a, (unsigned)b);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(ca, cb, msg);
        }
    }
}

static void test_every_pin_is_either_digital_or_adc(void)
{
    for (uint8_t i = 0u; i < AG_PIN_COUNT; i++) {
        uint8_t f = ag_board_pin_flags(i);
        TEST_ASSERT_TRUE_MESSAGE(f & (AG_PIN_DIGITAL | AG_PIN_ADC), "a pin that can do nothing");
    }
}

/* Unlike the 328P, whose A6 and A7 are ADC-only on the TQFP package, every ADC
 * input on this part is a real port pin. So no row may carry AG_PORT_NONE --
 * a pin that refused digital_write here would be a table error, not a package
 * limitation. */
static void test_no_pin_on_this_part_is_adc_only(void)
{
    for (uint8_t i = 0u; i < AG_PIN_COUNT; i++) {
        ag_pinref_t r;
        char        msg[48];
        snprintf(msg, sizeof msg, "pin %u has no digital port", (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE(AG_OK, ag_board_pin(i, &r), msg);
    }
}

/* The descriptor's own headline numbers, asserted rather than assumed: a
 * profile edit that changed the pin count without changing the table would
 * otherwise be caught only by the _Static_assert in board.c, which nobody sees
 * until the next firmware build. */
static void test_the_board_reports_its_identity(void)
{
    TEST_ASSERT_EQUAL_UINT8(24u, ag_board_pin_count());
    TEST_ASSERT_EQUAL_STRING("atmega32u4", ag_board_name());
    TEST_ASSERT_EQUAL_UINT8(0x03u, AG_BOARD_ID);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_digital_0_through_3_are_port_d_in_header_order);
    RUN_TEST(test_the_remaining_port_d_pins);
    RUN_TEST(test_port_c_carries_only_pins_5_and_13);
    RUN_TEST(test_port_e_carries_only_pin_7);
    RUN_TEST(test_port_b_carries_the_upper_digitals_and_the_spi_pins);
    RUN_TEST(test_analog_0_through_5_run_down_port_f);
    RUN_TEST(test_the_builtin_led_is_pin_13_on_port_c_bit_7);
    RUN_TEST(test_analog_pins_report_their_scattered_adc_channels);
    RUN_TEST(test_the_high_bank_channels_live_on_digital_pins);
    RUN_TEST(test_every_claimed_channel_is_within_the_hal_bound);
    RUN_TEST(test_no_pin_claims_a_channel_the_part_does_not_have);
    RUN_TEST(test_digital_only_pins_have_no_adc_channel);
    RUN_TEST(test_the_tick_timers_pins_are_reserved_not_merely_absent);
    RUN_TEST(test_the_timer4_pins_report_pwm_absent_not_reserved);
    RUN_TEST(test_pwm_pins_resolve_to_their_own_compare_unit);
    RUN_TEST(test_the_uart_pins_are_marked_in_use);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_no_two_pins_share_a_port_and_bit);
    RUN_TEST(test_no_two_pins_share_an_adc_channel);
    RUN_TEST(test_every_pin_is_either_digital_or_adc);
    RUN_TEST(test_no_pin_on_this_part_is_adc_only);
    RUN_TEST(test_the_board_reports_its_identity);
    return UNITY_END();
}
