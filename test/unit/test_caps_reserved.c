/* pwm.c's RESERVED term under mutation (#163), on test/mock/board_reserved.c:
 * the host library with a pin table whose D3 has a compare unit and is
 * RESERVED. On every shipped table the reserved pins have no compare unit, so
 * a dropped RESERVED term is still refused, by the PWM lookup, and survives
 * test_caps_mutate. */
#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "board_profile.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t pwm(uint8_t pin, uint8_t duty)
{
    uint8_t p[2] = { pin, duty };

    memcpy(inbuf, p, sizeof p);
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, (uint8_t)sizeof p);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_PWM_WRITE, &r, &w);
}

/* Kills: the RESERVED term dropped from pwm's mask (D3 would pass the PWM
 * lookup and be driven). */
static void test_pwm_on_a_reserved_compare_pin_is_refused_and_not_driven(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, pwm(3u, 128u));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_FALSE(tfw_mock_pwm_connected(TFW_ADDR_TCCR2A, COM2B1_BIT));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pwm_duty(TFW_ADDR_OCR2B));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* The table itself: an unreserved compare pin on it is driven, so the refusal
 * above is the flag's and not the table's. */
static void test_pwm_on_an_unreserved_compare_pin_is_driven(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, pwm(9u, 128u));
    TEST_ASSERT_EQUAL_UINT8(128u, tfw_mock_pwm_duty(TFW_ADDR_OCR1AL));
    TEST_ASSERT_TRUE(tfw_mock_pwm_connected(TFW_ADDR_TCCR1A, COM1A1_BIT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_on_a_reserved_compare_pin_is_refused_and_not_driven);
    RUN_TEST(test_pwm_on_an_unreserved_compare_pin_is_driven);
    return UNITY_END();
}
