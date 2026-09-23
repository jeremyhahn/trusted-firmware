/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATmega328P pin table, checked against the datasheet on x86.
 *
 * This is the payoff for putting the board descriptor above the HAL rather
 * than inside it. A wrong port bit for pin 13 is otherwise findable only by
 * wiring up a board and noticing the wrong LED -- and only if someone is
 * looking at that particular pin. Here it is a failing test on a laptop.
 *
 * The addresses are the real ones: PINB 0x23, PINC 0x26, PIND 0x29.
 */

#include "unity.h"
#include "ag/board.h"
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

/* D0..D7 are PORTD bits 0..7, in order. */
static void test_digital_0_through_7_map_to_port_d(void)
{
    for (uint8_t i = 0u; i <= 7u; i++) {
        expect_pin(i, AG_ADDR_PIND, i);
    }
}

/* D8..D13 are PORTB bits 0..5. The discontinuity at D8 is the one people get
 * wrong, because the pin numbers stay consecutive while the port changes. */
static void test_digital_8_through_13_map_to_port_b(void)
{
    for (uint8_t i = 8u; i <= 13u; i++) {
        expect_pin(i, AG_ADDR_PINB, (uint8_t)(i - 8u));
    }
}

/* A0..A5 are PORTC bits 0..5, reachable as digital pins 14..19. */
static void test_analog_0_through_5_map_to_port_c(void)
{
    for (uint8_t i = 14u; i <= 19u; i++) {
        expect_pin(i, AG_ADDR_PINC, (uint8_t)(i - 14u));
    }
}

/* The built-in LED is pin 13, PORTB bit 5. Stated separately because it is the
 * pin every bring-up test blinks, and getting it wrong wastes an afternoon. */
static void test_the_builtin_led_is_pin_13_on_port_b_bit_5(void)
{
    expect_pin(13u, AG_ADDR_PINB, 5u);
    TEST_ASSERT_TRUE(ag_board_pin_flags(13u) & AG_PIN_LED);
}

/* A6 and A7 exist only on the TQFP package and have no digital port at all.
 * digital_write to either must FAIL rather than write into a port that is not
 * there -- which, with a naive table, would be port index 0 and would toggle
 * something on PORTB instead. */
static void test_adc_only_pins_cannot_be_used_as_digital(void)
{
    ag_pinref_t r;
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_board_pin(20u, &r));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_board_pin(21u, &r));
}

static void test_adc_only_pins_still_have_adc_channels(void)
{
    uint8_t ch;
    TEST_ASSERT_EQUAL(AG_OK, ag_board_adc_channel(20u, &ch));
    TEST_ASSERT_EQUAL_UINT8(6u, ch);
    TEST_ASSERT_EQUAL(AG_OK, ag_board_adc_channel(21u, &ch));
    TEST_ASSERT_EQUAL_UINT8(7u, ch);
}

static void test_analog_pins_report_their_adc_channel(void)
{
    for (uint8_t i = 14u; i <= 19u; i++) {
        uint8_t ch;
        TEST_ASSERT_EQUAL(AG_OK, ag_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i - 14u), ch);
    }
}

static void test_digital_pins_have_no_adc_channel(void)
{
    uint8_t ch;
    for (uint8_t i = 0u; i <= 13u; i++) {
        TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_board_adc_channel(i, &ch));
    }
}

/* Timer0 is the millisecond tick, so pins 5 and 6 cannot do PWM even though
 * they have output-compare units. Reported as reserved rather than absent:
 * "no PWM here" and "this build spent it on the clock" are different facts,
 * and only one of them might change. */
static void test_the_tick_timers_pins_are_reserved_not_merely_absent(void)
{
    TEST_ASSERT_FALSE(ag_board_pin_flags(5u) & AG_PIN_PWM);
    TEST_ASSERT_FALSE(ag_board_pin_flags(6u) & AG_PIN_PWM);
    /* while the four genuine PWM pins are advertised */
    TEST_ASSERT_TRUE(ag_board_pin_flags(3u)  & AG_PIN_PWM);
    TEST_ASSERT_TRUE(ag_board_pin_flags(9u)  & AG_PIN_PWM);
    TEST_ASSERT_TRUE(ag_board_pin_flags(10u) & AG_PIN_PWM);
    TEST_ASSERT_TRUE(ag_board_pin_flags(11u) & AG_PIN_PWM);
}

/* Pins 0 and 1 are the UART this firmware is talking over. A host that drove
 * them would cut the branch it is sitting on. */
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
    TEST_ASSERT_EQUAL_UINT8(0u, ag_board_pin_flags(AG_PIN_COUNT));
}

/* No two pins may resolve to the same port and bit. A copy-paste slip in the
 * table is otherwise silent: both pins work, and they work as each other. */
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

/*
 * THE PIN PAGE'S TWO BYTES, which CAPS reports verbatim and which the first
 * version of that code could not produce.
 *
 * The port byte was built from ag_pinref_t, whose `port` field is the PINx
 * MEMORY ADDRESS rather than a port index -- so the index was not recoverable
 * and a constant high nibble went out for every pin on every board. The bit was
 * right, which is what made it survive review.
 */
static void test_the_packed_port_byte_is_the_descriptors_own_packing(void)
{
    TEST_ASSERT_EQUAL_HEX8(AG_PB(0u, 5u), ag_board_pin_port_bit(13u));  /* PB5, the LED */
    TEST_ASSERT_EQUAL_HEX8(AG_PB(2u, 0u), ag_board_pin_port_bit(0u));   /* PD0, RX */
    TEST_ASSERT_EQUAL_HEX8(AG_PB(1u, 0u), ag_board_pin_port_bit(14u));  /* PC0, A0 */
}

/* A6 and A7 are ADC-only on the TQFP package and absent from the DIP. The high
 * nibble says so, and that is the only way a host can learn a pin exists and
 * cannot be written. */
static void test_an_adc_only_pin_reports_no_port(void)
{
    TEST_ASSERT_EQUAL_HEX8(AG_PORT_NONE, (uint8_t)(ag_board_pin_port_bit(20u) >> 4));
    TEST_ASSERT_EQUAL_HEX8(AG_PORT_NONE, (uint8_t)(ag_board_pin_port_bit(21u) >> 4));
}

static void test_a_pin_beyond_the_board_has_no_packed_port(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xFFu, ag_board_pin_port_bit(99u));
}

/*
 * STATE 2 IS REACHABLE, which is the whole point of having three states.
 *
 * The first version derived it from AG_PIN_PWM in the flags -- but a pin whose
 * timer went to the tick carries only AG_PIN_DIGITAL, precisely because a host
 * must not try to drive it. So state 2 could never be emitted, and pins 5 and 6
 * reported "no compare unit" when what is true is "this build spent it on the
 * clock". The distinction exists so a host knows the fix is a different build
 * rather than a different pin, and reporting 0 gives exactly the wrong advice.
 */
static void test_the_tick_timers_pins_report_reserved_not_absent(void)
{
    TEST_ASSERT_EQUAL_UINT8(2u, ag_board_pin_pwm_state(5u));   /* OC0B, tick */
    TEST_ASSERT_EQUAL_UINT8(2u, ag_board_pin_pwm_state(6u));   /* OC0A, tick */
}

static void test_a_usable_compare_unit_reports_available(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, ag_board_pin_pwm_state(3u));   /* OC2B */
    TEST_ASSERT_EQUAL_UINT8(1u, ag_board_pin_pwm_state(9u));   /* OC1A */
}

static void test_a_pin_with_no_compare_unit_reports_none(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, ag_board_pin_pwm_state(2u));
    TEST_ASSERT_EQUAL_UINT8(0u, ag_board_pin_pwm_state(99u));  /* and one that does not exist */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_digital_0_through_7_map_to_port_d);
    RUN_TEST(test_digital_8_through_13_map_to_port_b);
    RUN_TEST(test_analog_0_through_5_map_to_port_c);
    RUN_TEST(test_the_builtin_led_is_pin_13_on_port_b_bit_5);
    RUN_TEST(test_adc_only_pins_cannot_be_used_as_digital);
    RUN_TEST(test_adc_only_pins_still_have_adc_channels);
    RUN_TEST(test_analog_pins_report_their_adc_channel);
    RUN_TEST(test_digital_pins_have_no_adc_channel);
    RUN_TEST(test_the_tick_timers_pins_are_reserved_not_merely_absent);
    RUN_TEST(test_the_uart_pins_are_marked_in_use);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_no_two_pins_share_a_port_and_bit);
    RUN_TEST(test_no_two_pins_share_an_adc_channel);
    RUN_TEST(test_every_pin_is_either_digital_or_adc);
    RUN_TEST(test_the_packed_port_byte_is_the_descriptors_own_packing);
    RUN_TEST(test_an_adc_only_pin_reports_no_port);
    RUN_TEST(test_a_pin_beyond_the_board_has_no_packed_port);
    RUN_TEST(test_the_tick_timers_pins_report_reserved_not_absent);
    RUN_TEST(test_a_usable_compare_unit_reports_available);
    RUN_TEST(test_a_pin_with_no_compare_unit_reports_none);
    return UNITY_END();
}
