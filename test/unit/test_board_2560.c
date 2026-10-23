/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ATmega2560 pin table, checked against the datasheet on x86.
 *
 * The Mega earns this test more than any other board here. Its header numbers
 * bear almost no relation to its port layout -- the first fourteen pins touch
 * four different ports, two blocks of eight count DOWN their port, and the
 * ports themselves skip the letter I -- so there is no rule a reader can apply
 * to check the table by eye. Every line is an independent chance to be wrong.
 *
 * Getting one wrong on a bench looks like a dead output, or worse, like the
 * wrong output moving. Here it is a failing test on a laptop.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"
#include "tfw/config.h"

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

/* D0..D5 are the one stretch where consecutive header pins are neither
 * consecutive bits nor even the same port: PE0, PE1, PE4, PE5, PG5, PE3. The
 * jump to port G for pin 4 alone is the single most surprising entry in the
 * table, and it is there because PG5 is where OC0B comes out. */
static void test_digital_0_through_5_zigzag_across_ports_e_and_g(void)
{
    expect_pin(0u, TFW_ADDR_PINE, 0u);
    expect_pin(1u, TFW_ADDR_PINE, 1u);
    expect_pin(2u, TFW_ADDR_PINE, 4u);
    expect_pin(3u, TFW_ADDR_PINE, 5u);
    expect_pin(4u, TFW_ADDR_PING, 5u);
    expect_pin(5u, TFW_ADDR_PINE, 3u);
}

/* D6..D9 are PORTH bits 3..6, the four PWM pins from Timer4 and Timer2. */
static void test_digital_6_through_9_map_to_port_h(void)
{
    for (uint8_t i = 6u; i <= 9u; i++) {
        expect_pin(i, TFW_ADDR_PINH, (uint8_t)(i - 3u));
    }
}

/* D10..D13 are PORTB bits 4..7 -- note bits 4..7, not 0..3. Port B's low four
 * bits are the SPI pins at the far end of the header, D50..D53. */
static void test_digital_10_through_13_map_to_the_high_half_of_port_b(void)
{
    for (uint8_t i = 10u; i <= 13u; i++) {
        expect_pin(i, TFW_ADDR_PINB, (uint8_t)(i - 6u));
    }
}

/* D14..D17 are the TX/RX pairs of USART3 and USART2, and each pair is printed
 * TX first -- so the bit numbers run backwards within the pair. */
static void test_the_spare_usart_pins_are_on_ports_j_and_h(void)
{
    expect_pin(14u, TFW_ADDR_PINJ, 1u);   /* TX3 */
    expect_pin(15u, TFW_ADDR_PINJ, 0u);   /* RX3 */
    expect_pin(16u, TFW_ADDR_PINH, 1u);   /* TX2 */
    expect_pin(17u, TFW_ADDR_PINH, 0u);   /* RX2 */
}

/* D18..D21 are PORTD bits 3,2,1,0: USART1 then I2C, counting down. */
static void test_digital_18_through_21_count_down_port_d(void)
{
    for (uint8_t i = 18u; i <= 21u; i++) {
        expect_pin(i, TFW_ADDR_PIND, (uint8_t)(21u - i));
    }
}

/* D22..D29 are PORTA bits 0..7, the one block that runs the way everyone
 * expects. */
static void test_digital_22_through_29_map_to_port_a_in_order(void)
{
    for (uint8_t i = 22u; i <= 29u; i++) {
        expect_pin(i, TFW_ADDR_PINA, (uint8_t)(i - 22u));
    }
}

/* D30..D37 are PORTC bits 7 down to 0. The reversal is real and is printed that
 * way on the header; a table that ran it upwards would work perfectly and drive
 * the wrong eight pins. */
static void test_digital_30_through_37_count_down_port_c(void)
{
    for (uint8_t i = 30u; i <= 37u; i++) {
        expect_pin(i, TFW_ADDR_PINC, (uint8_t)(37u - i));
    }
}

/* D38 goes back to port D, and D39..D41 are port G counting down -- three pins
 * that belong to neither of the blocks on either side of them. */
static void test_digital_38_through_41_are_the_leftovers_of_ports_d_and_g(void)
{
    expect_pin(38u, TFW_ADDR_PIND, 7u);
    expect_pin(39u, TFW_ADDR_PING, 2u);
    expect_pin(40u, TFW_ADDR_PING, 1u);
    expect_pin(41u, TFW_ADDR_PING, 0u);
}

/* D42..D49 are PORTL bits 7 down to 0, the second descending block. */
static void test_digital_42_through_49_count_down_port_l(void)
{
    for (uint8_t i = 42u; i <= 49u; i++) {
        expect_pin(i, TFW_ADDR_PINL, (uint8_t)(49u - i));
    }
}

/* D50..D53 are the SPI pins on PORTB bits 3 down to 0, which is why D10..D13
 * sit on bits 4..7 rather than at the bottom of the port. */
static void test_the_spi_pins_count_down_the_low_half_of_port_b(void)
{
    expect_pin(50u, TFW_ADDR_PINB, 3u);   /* MISO */
    expect_pin(51u, TFW_ADDR_PINB, 2u);   /* MOSI */
    expect_pin(52u, TFW_ADDR_PINB, 1u);   /* SCK  */
    expect_pin(53u, TFW_ADDR_PINB, 0u);   /* SS   */
}

/* A0..A7 are PORTF bits 0..7, reachable as digital pins 54..61. */
static void test_analog_0_through_7_map_to_port_f(void)
{
    for (uint8_t i = 54u; i <= 61u; i++) {
        expect_pin(i, TFW_ADDR_PINF, (uint8_t)(i - 54u));
    }
}

/* A8..A15 are PORTK bits 0..7, digital pins 62..69. Port K is in the extended
 * I/O space above 0xFF, which is the half of the address range the 328P never
 * uses -- a port descriptor truncated to a byte would land these on port A. */
static void test_analog_8_through_15_map_to_port_k(void)
{
    for (uint8_t i = 62u; i <= 69u; i++) {
        expect_pin(i, TFW_ADDR_PINK, (uint8_t)(i - 62u));
    }
}

/* The built-in LED is pin 13, PORTB bit 7 -- not bit 5, which is where it sits
 * on the 328P and where a table copied from that board would leave it. Stated
 * separately because it is the pin every bring-up test blinks. */
static void test_the_builtin_led_is_pin_13_on_port_b_bit_7(void)
{
    expect_pin(13u, TFW_ADDR_PINB, 7u);
    TEST_ASSERT_TRUE(tfw_board_pin_flags(13u) & TFW_PIN_LED);
}

static void test_analog_pins_report_their_adc_channel(void)
{
    for (uint8_t i = 54u; i <= 69u; i++) {
        uint8_t ch;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(i - 54u), ch);
    }
}

/* Channels 8..15 are the ones that live behind MUX5 in ADCSRB. The table must
 * hand the HAL the true channel number rather than a folded one, because it is
 * the HAL that knows about the bank bit. */
static void test_the_high_adc_channels_are_numbered_8_through_15(void)
{
    uint8_t ch;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(62u, &ch));
    TEST_ASSERT_EQUAL_UINT8(8u, ch);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_adc_channel(69u, &ch));
    TEST_ASSERT_EQUAL_UINT8(15u, ch);
}

static void test_digital_pins_have_no_adc_channel(void)
{
    uint8_t ch;
    for (uint8_t i = 0u; i <= 53u; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
    }
}

/* Timer0 is the millisecond tick, so pins 4 (OC0B on PG5) and 13 (OC0A on PB7)
 * cannot do PWM even though they have output-compare units. Reported as
 * reserved rather than absent: "no PWM here" and "this build spent it on the
 * clock" are different facts, and only one of them might change. */
static void test_the_tick_timers_pins_are_reserved_not_merely_absent(void)
{
    tfw_pwmref_t r;

    TEST_ASSERT_FALSE(tfw_board_pin_flags(4u)  & TFW_PIN_PWM);
    TEST_ASSERT_FALSE(tfw_board_pin_flags(13u) & TFW_PIN_PWM);
    /* Reserved, not simply missing: both pins refuse a duty cycle, and they do
     * so because the table holds TFW_PWM_RSVD rather than TFW_PWM_NONE. */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(4u,  &r));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(13u, &r));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_board_pin_pwm_state(4u));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_board_pin_pwm_state(13u));
}

/* The thirteen genuine PWM pins, listed rather than derived: 2, 3 and 5..12 in
 * the low block and 44..46 on port L. A derived list would agree with whatever
 * the table says and prove nothing. */
static void test_every_genuine_pwm_pin_is_advertised(void)
{
    static const uint8_t pwm_pins[] = { 2u, 3u, 5u, 6u, 7u, 8u, 9u, 10u,
                                        11u, 12u, 44u, 45u, 46u };
    size_t i;

    TEST_ASSERT_EQUAL_UINT(TFW_PWM_COUNT, sizeof pwm_pins / sizeof pwm_pins[0]);
    for (i = 0u; i < sizeof pwm_pins / sizeof pwm_pins[0]; i++) {
        tfw_pwmref_t r;
        char        msg[48];
        snprintf(msg, sizeof msg, "pin %u", (unsigned)pwm_pins[i]);
        TEST_ASSERT_TRUE_MESSAGE(tfw_board_pin_flags(pwm_pins[i]) & TFW_PIN_PWM, msg);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_board_pwm(pwm_pins[i], &r), msg);
    }
}

/* No two pins may point at the same compare register. Five timers and thirteen
 * units is enough rows for a copy-pasted OCR address to go unnoticed, and the
 * symptom would be two pins that dim together. */
static void test_no_two_pins_share_an_output_compare_unit(void)
{
    for (uint8_t a = 0u; a < TFW_PIN_COUNT; a++) {
        tfw_pwmref_t ra;
        if (tfw_board_pwm(a, &ra) != TFW_OK) { continue; }
        for (uint8_t b = (uint8_t)(a + 1u); b < TFW_PIN_COUNT; b++) {
            tfw_pwmref_t rb;
            char        msg[64];
            if (tfw_board_pwm(b, &rb) != TFW_OK) { continue; }
            snprintf(msg, sizeof msg, "pins %u and %u share a compare unit",
                     (unsigned)a, (unsigned)b);
            TEST_ASSERT_FALSE_MESSAGE(ra.duty == rb.duty && ra.bit == rb.bit, msg);
        }
    }
}

/* Pins 0 and 1 are USART0, the line this firmware is talking over. A host that
 * drove them would cut the branch it is sitting on. The Mega's other three
 * USARTs are free, and must not be marked as though they were not. */
static void test_the_uart_pins_are_marked_in_use(void)
{
    TEST_ASSERT_TRUE(tfw_board_pin_flags(0u) & TFW_PIN_IN_USE);
    TEST_ASSERT_TRUE(tfw_board_pin_flags(1u) & TFW_PIN_IN_USE);
    TEST_ASSERT_FALSE(tfw_board_pin_flags(14u) & TFW_PIN_IN_USE);
    TEST_ASSERT_FALSE(tfw_board_pin_flags(19u) & TFW_PIN_IN_USE);
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;
    uint8_t     ch;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(255u, &r));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_adc_channel(TFW_PIN_COUNT, &ch));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_flags(TFW_PIN_COUNT));
}

/* No two pins may resolve to the same port and bit. A copy-paste slip in a
 * seventy-row table is otherwise silent: both pins work, and they work as each
 * other. */
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
            snprintf(msg, sizeof msg, "pins %u and %u share ADC channel",
                     (unsigned)a, (unsigned)b);
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

/* Unlike the 328P, this part has no ADC-only pin: every one of the seventy has
 * a real digital port, so tfw_board_pin must succeed for all of them. A missing
 * row would zero-fill to port A bit 0 and pass everything except this. */
static void test_every_pin_has_a_digital_port(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;
        char        msg[48];
        snprintf(msg, sizeof msg, "pin %u", (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_board_pin(i, &r), msg);
        TEST_ASSERT_TRUE_MESSAGE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL, msg);
    }
}

/* #30: the Mega's element image is the one that carries ATTEST_CSR -- the only
 * part with the flash for it -- and CAPS bit 15 says so. Compiled with the
 * element tier's own flags (`make board-sweep`); config.h alone would say 0. */
static void test_the_element_image_carries_attest_csr(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_ALIAS_CERT, "the template ATTEST_CSR reads");
    TEST_ASSERT_EQUAL_MESSAGE(1, TFW_FEATURE_ATTEST_CSR, "the one part enrolled at ACME finalize");
    TEST_ASSERT_EQUAL_HEX32(1uL << 15, TFW_FEATURE_BITMAP & (1uL << 15));
}

/* The MIKROE-6774 Click's element: the ATECC608C-TFLXTLS at 0x36 as shipped
 * (DS40002515B section 3.1), at 100 kHz (table 10-2). The Mega's TWI is D20
 * (PD1, SDA) and D21 (PD0, SCL); its A4/A5 are plain analog inputs. */
static void test_the_click_element_is_on_the_twi_pins_at_0x36(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x36u, TFW_ELEMENT_I2C_ADDR);
    TEST_ASSERT_EQUAL_UINT32(100000uL, TFW_ELEMENT_I2C_HZ);
    TEST_ASSERT_EQUAL_UINT8(20u, TFW_ELEMENT_PIN_SDA);
    TEST_ASSERT_EQUAL_UINT8(21u, TFW_ELEMENT_PIN_SCL);
    expect_pin(TFW_ELEMENT_PIN_SDA, TFW_ADDR_PIND, 1u);
    expect_pin(TFW_ELEMENT_PIN_SCL, TFW_ADDR_PIND, 0u);
}

/* Clear of the Ethernet shield: D10 (W5x00 select), D4 (SD select) and the
 * Mega's SPI on D50-D53. */
static void test_the_click_element_does_not_collide_with_the_ethernet_shield(void)
{
    static const uint8_t shield[] = { 4u, 10u, 50u, 51u, 52u, 53u };
    uint8_t              i;

    TEST_ASSERT_EQUAL_UINT8(10u, TFW_ETH_CS_PIN);
    TEST_ASSERT_EQUAL_UINT8(4u,  TFW_ETH_SD_CS_PIN);
    for (i = 0u; i < sizeof shield; i++) {
        TEST_ASSERT_NOT_EQUAL(shield[i], TFW_ELEMENT_PIN_SDA);
        TEST_ASSERT_NOT_EQUAL(shield[i], TFW_ELEMENT_PIN_SCL);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_click_element_is_on_the_twi_pins_at_0x36);
    RUN_TEST(test_the_click_element_does_not_collide_with_the_ethernet_shield);
    RUN_TEST(test_digital_0_through_5_zigzag_across_ports_e_and_g);
    RUN_TEST(test_digital_6_through_9_map_to_port_h);
    RUN_TEST(test_digital_10_through_13_map_to_the_high_half_of_port_b);
    RUN_TEST(test_the_spare_usart_pins_are_on_ports_j_and_h);
    RUN_TEST(test_digital_18_through_21_count_down_port_d);
    RUN_TEST(test_digital_22_through_29_map_to_port_a_in_order);
    RUN_TEST(test_digital_30_through_37_count_down_port_c);
    RUN_TEST(test_digital_38_through_41_are_the_leftovers_of_ports_d_and_g);
    RUN_TEST(test_digital_42_through_49_count_down_port_l);
    RUN_TEST(test_the_spi_pins_count_down_the_low_half_of_port_b);
    RUN_TEST(test_analog_0_through_7_map_to_port_f);
    RUN_TEST(test_analog_8_through_15_map_to_port_k);
    RUN_TEST(test_the_builtin_led_is_pin_13_on_port_b_bit_7);
    RUN_TEST(test_analog_pins_report_their_adc_channel);
    RUN_TEST(test_the_high_adc_channels_are_numbered_8_through_15);
    RUN_TEST(test_digital_pins_have_no_adc_channel);
    RUN_TEST(test_the_tick_timers_pins_are_reserved_not_merely_absent);
    RUN_TEST(test_every_genuine_pwm_pin_is_advertised);
    RUN_TEST(test_no_two_pins_share_an_output_compare_unit);
    RUN_TEST(test_the_uart_pins_are_marked_in_use);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_no_two_pins_share_a_port_and_bit);
    RUN_TEST(test_no_two_pins_share_an_adc_channel);
    RUN_TEST(test_every_pin_is_either_digital_or_adc);
    RUN_TEST(test_every_pin_has_a_digital_port);
    RUN_TEST(test_the_element_image_carries_attest_csr);
    return UNITY_END();
}
