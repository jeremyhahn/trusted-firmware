/* Direct unit test of src/core/board_lookup.c against the host board's tables.
 *
 * Each refusal path is reached through a pin the table itself marks for it, so
 * the suite follows HOST_BOARD; a board that lacks such a pin fails the test
 * rather than skipping it. */

#include <string.h>
#include "unity.h"
#include "tfw/board.h"

void setUp(void) {}
void tearDown(void) {}

static const tfw_pin_desc_t *desc(tfw_pin_t pin) { return &tfw_board_pins[pin]; }

static uint8_t port_of(tfw_pin_t pin)
{
    return tfw_pin_coord_port(tfw_pin_coord_load(&desc(pin)->port_bit));
}

static uint8_t bit_of(tfw_pin_t pin)
{
    return tfw_pin_coord_bit(tfw_pin_coord_load(&desc(pin)->port_bit));
}

/* tfw_board_pin_count, tfw_board_name: the board profile's constants. */
static void test_pin_count_is_the_profile_count(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_PIN_COUNT, tfw_board_pin_count());
    TEST_ASSERT_TRUE(tfw_board_pin_count() > 0u);
}

static void test_board_name_is_the_profile_name(void)
{
    TEST_ASSERT_NOT_NULL(tfw_board_name());
    TEST_ASSERT_EQUAL_STRING(TFW_BOARD_NAME, tfw_board_name());
}

/* tfw_board_pin */
static void test_pin_resolves_every_pin_with_a_port(void)
{
    tfw_pinref_t ref;
    tfw_pin_t    p;
    uint8_t      found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (port_of(p) == TFW_PORT_NONE) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pin(p, &ref));
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_pin_first_and_last_entries(void)
{
    tfw_pinref_t ref;
    tfw_pin_t    last = (tfw_pin_t)(TFW_PIN_COUNT - 1u);

    TEST_ASSERT_EQUAL_INT(port_of(0) == TFW_PORT_NONE ? TFW_ERR_UNSUPPORTED : TFW_OK,
                          tfw_board_pin(0, &ref));
    TEST_ASSERT_EQUAL_INT(port_of(last) == TFW_PORT_NONE ? TFW_ERR_UNSUPPORTED : TFW_OK,
                          tfw_board_pin(last, &ref));
}

static void test_pin_refuses_a_pin_past_the_table(void)
{
    tfw_pinref_t ref;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_board_pin((tfw_pin_t)TFW_PIN_COUNT, &ref));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_board_pin((tfw_pin_t)0xFFu, &ref));
}

static void test_pin_refuses_a_pin_without_a_digital_port(void)
{
    tfw_pinref_t ref;
    tfw_pin_t    p;
    uint8_t      found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (port_of(p) != TFW_PORT_NONE) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_board_pin(p, &ref));
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

/* tfw_board_adc_channel */
static void test_adc_channel_returns_the_stored_channel(void)
{
    tfw_pin_t p;
    uint8_t   ch, found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (desc(p)->adc_ch == TFW_ADC_NONE) { continue; }
        ch = 0xA5u;
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_adc_channel(p, &ch));
        TEST_ASSERT_EQUAL_UINT8(desc(p)->adc_ch, ch);
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_adc_channel_refuses_a_pin_without_one_and_leaves_out(void)
{
    tfw_pin_t p;
    uint8_t   ch, found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (desc(p)->adc_ch != TFW_ADC_NONE) { continue; }
        ch = 0xA5u;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(p, &ch));
        TEST_ASSERT_EQUAL_UINT8(0xA5u, ch);
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_adc_channel_refuses_a_pin_past_the_table(void)
{
    uint8_t ch = 0xA5u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_board_adc_channel((tfw_pin_t)TFW_PIN_COUNT, &ch));
    TEST_ASSERT_EQUAL_UINT8(0xA5u, ch);
}

/* tfw_board_pwm and tfw_board_pin_pwm_state */
static void test_pwm_resolves_a_pin_with_a_usable_timer(void)
{
    tfw_pwmref_t ref;
    tfw_pin_t    p;
    uint8_t      found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        uint8_t idx = desc(p)->pwm_idx;
        if (idx == TFW_PWM_NONE || idx == TFW_PWM_RSVD) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pwm(p, &ref));
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_board_pin_pwm_state(p));
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_pwm_refuses_a_pin_with_no_compare_unit(void)
{
    tfw_pwmref_t ref;
    tfw_pin_t    p;
    uint8_t      found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (desc(p)->pwm_idx != TFW_PWM_NONE) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_board_pwm(p, &ref));
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state(p));
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

/* RSVD is the same refusal as NONE to tfw_board_pwm (board_lookup.c:75); only
 * the CAPS state byte tells them apart. */
static void test_pwm_refuses_a_reserved_timer_pin_but_reports_it_as_2(void)
{
    tfw_pwmref_t ref;
    tfw_pin_t    p;
    uint8_t      found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (desc(p)->pwm_idx != TFW_PWM_RSVD) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_board_pwm(p, &ref));
        TEST_ASSERT_EQUAL_UINT8(2u, tfw_board_pin_pwm_state(p));
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_pwm_refuses_a_pin_past_the_table(void)
{
    tfw_pwmref_t ref;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_board_pwm((tfw_pin_t)TFW_PIN_COUNT, &ref));
}

/* Out of range reads as 0, the same byte as "no PWM": the state has no
 * bad-pin value (board_lookup.c:92). */
static void test_pwm_state_of_a_pin_past_the_table_is_0(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state((tfw_pin_t)TFW_PIN_COUNT));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_pwm_state((tfw_pin_t)0xFFu));
}

/* tfw_board_pin_flags */
static void test_pin_flags_are_the_stored_flags(void)
{
    tfw_pin_t p;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT8(desc(p)->flags, tfw_board_pin_flags(p));
    }
}

/* Out of range reads as 0, indistinguishable from a pin with no flags
 * (board_lookup.c:125). */
static void test_pin_flags_of_a_pin_past_the_table_are_0(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_board_pin_flags((tfw_pin_t)TFW_PIN_COUNT));
}

/* tfw_board_pin_page */
static void test_pin_page_of_every_pin_with_a_port(void)
{
    uint8_t   page[6];
    tfw_pin_t p;
    uint8_t   found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (port_of(p) == TFW_PORT_NONE) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pin_page(p, page));
        TEST_ASSERT_EQUAL_UINT8(p, page[0]);
        TEST_ASSERT_EQUAL_UINT8(desc(p)->flags, page[1]);
        TEST_ASSERT_EQUAL_UINT8(port_of(p), page[2]);
        TEST_ASSERT_EQUAL_UINT8(bit_of(p), page[3]);
        TEST_ASSERT_EQUAL_UINT8(desc(p)->adc_ch, page[4]);
        TEST_ASSERT_EQUAL_UINT8(tfw_board_pin_pwm_state(p), page[5]);
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

/* Unlike tfw_board_pin, the page does not refuse a portless pin: it answers
 * TFW_OK with 0xFF in both the port and the bit byte (board_lookup.c:109). */
static void test_pin_page_of_a_portless_pin_is_ok_with_wire_none(void)
{
    uint8_t   page[6];
    tfw_pin_t p;
    uint8_t   found = 0u;

    for (p = 0; p < TFW_PIN_COUNT; p++) {
        if (port_of(p) != TFW_PORT_NONE) { continue; }
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pin_page(p, page));
        TEST_ASSERT_EQUAL_UINT8(p, page[0]);
        TEST_ASSERT_EQUAL_UINT8(TFW_WIRE_PORT_NONE, page[2]);
        TEST_ASSERT_EQUAL_UINT8(TFW_WIRE_PORT_NONE, page[3]);
        TEST_ASSERT_EQUAL_UINT8(desc(p)->adc_ch, page[4]);
        found++;
    }
    TEST_ASSERT_TRUE(found > 0u);
}

static void test_pin_page_last_entry(void)
{
    uint8_t   page[6];
    tfw_pin_t last = (tfw_pin_t)(TFW_PIN_COUNT - 1u);

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_board_pin_page(last, page));
    TEST_ASSERT_EQUAL_UINT8(last, page[0]);
}

static void test_pin_page_refuses_a_pin_past_the_table_and_writes_nothing(void)
{
    uint8_t page[6];
    uint8_t clean[6];

    memset(page, 0xA5, sizeof page);
    memset(clean, 0xA5, sizeof clean);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_board_pin_page((tfw_pin_t)TFW_PIN_COUNT, page));
    TEST_ASSERT_EQUAL_MEMORY(clean, page, sizeof page);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pin_count_is_the_profile_count);
    RUN_TEST(test_board_name_is_the_profile_name);
    RUN_TEST(test_pin_resolves_every_pin_with_a_port);
    RUN_TEST(test_pin_first_and_last_entries);
    RUN_TEST(test_pin_refuses_a_pin_past_the_table);
    RUN_TEST(test_pin_refuses_a_pin_without_a_digital_port);
    RUN_TEST(test_adc_channel_returns_the_stored_channel);
    RUN_TEST(test_adc_channel_refuses_a_pin_without_one_and_leaves_out);
    RUN_TEST(test_adc_channel_refuses_a_pin_past_the_table);
    RUN_TEST(test_pwm_resolves_a_pin_with_a_usable_timer);
    RUN_TEST(test_pwm_refuses_a_pin_with_no_compare_unit);
    RUN_TEST(test_pwm_refuses_a_reserved_timer_pin_but_reports_it_as_2);
    RUN_TEST(test_pwm_refuses_a_pin_past_the_table);
    RUN_TEST(test_pwm_state_of_a_pin_past_the_table_is_0);
    RUN_TEST(test_pin_flags_are_the_stored_flags);
    RUN_TEST(test_pin_flags_of_a_pin_past_the_table_are_0);
    RUN_TEST(test_pin_page_of_every_pin_with_a_port);
    RUN_TEST(test_pin_page_of_a_portless_pin_is_ok_with_wire_none);
    RUN_TEST(test_pin_page_last_entry);
    RUN_TEST(test_pin_page_refuses_a_pin_past_the_table_and_writes_nothing);
    return UNITY_END();
}
