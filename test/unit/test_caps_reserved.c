/* pwm.c's RESERVED term under mutation (#163), on test/mock/board_reserved.c:
 * the host library with a pin table whose D3 has a compare unit and is
 * RESERVED. On every shipped table the reserved pins have no compare unit, so
 * a dropped RESERVED term is still refused, by the PWM lookup, and survives
 * test_caps_mutate. gpio.c's RESERVED term (#627) too: the host 328P table's
 * refused pins are IN_USE only, so PIN_MODE and DIGITAL_WRITE on D3 here are
 * the only host check that a RESERVED pin is never driven. tone.c's RESERVED
 * term (#627) as well: the 328P tone pin D11 is unreserved on the board table,
 * so a TONE on this table's RESERVED D11 is the only host check of it. And
 * bus.c's SPI chip-select and 1-Wire and sched.c's TIMER_SET RESERVED terms
 * (#627): SPI_XFER, ONEWIRE_* and TIMER_SET on D3 here are their only host
 * check. */
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

static tfw_status_t run2(uint8_t cmd, uint8_t a, uint8_t b)
{
    uint8_t p[2] = { a, b };

    memcpy(inbuf, p, sizeof p);
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, (uint8_t)sizeof p);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

/* Kills: the RESERVED term dropped from gpio.c's resolve_writable mask (line
 * 24); on the 328P table the refused pins are IN_USE only. */
static void test_pin_mode_and_digital_write_on_a_reserved_pin_are_refused_and_it_stays(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                      run2(TFW_CMD_PIN_MODE, 3u, (uint8_t)TFW_PIN_OUTPUT));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, run2(TFW_CMD_DIGITAL_WRITE, 3u, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the RESERVED term dropped from tone.c's mask (line 33); the 328P tone
 * pin D11 is unreserved on the board table, so only this table's RESERVED D11
 * shows it. */
static void test_tone_on_a_reserved_tone_pin_is_refused_and_silent(void)
{
    uint8_t p[3] = { (uint8_t)TFW_TONE_PIN, 0xB8u, 0x01u }; /* 440 Hz, u16 LE */

    memcpy(inbuf, p, sizeof p);
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, (uint8_t)sizeof p);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN, tfw_dispatch(TFW_CMD_TONE, &r, &w));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

static tfw_status_t runp(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    memcpy(inbuf, p, n);
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

/* Kills: the RESERVED term dropped from bus.c's SPI chip-select mask (line
 * 145); on the 328P table the refused pins are IN_USE only. */
static void test_spi_xfer_with_a_reserved_chip_select_is_refused_with_no_reply(void)
{
    static const uint8_t resp[2] = { 0x5Au, 0xA5u };
    static const uint8_t p[3]    = { 3u, 0x9Fu, 0x00u };

    tfw_mock_set_bus_response(resp, (uint8_t)sizeof resp);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION,
                      runp(TFW_CMD_SPI_XFER, p, (uint8_t)sizeof p));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* Kills: the RESERVED term dropped from bus.c's 1-Wire masks (reset line 192,
 * write line 219, read line 244); on the 328P table the refused pins are
 * IN_USE only. */
static void test_onewire_on_a_reserved_pin_is_refused_and_never_reaches_the_bus(void)
{
    static const uint8_t rst[1] = { 3u };
    static const uint8_t wr[2]  = { 3u, 0xCCu };
    static const uint8_t rd[2]  = { 3u, 2u };

    (void)tfw_mock_ow_presence_set(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                      runp(TFW_CMD_ONEWIRE_RESET, rst, (uint8_t)sizeof rst));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                      runp(TFW_CMD_ONEWIRE_WRITE, wr, (uint8_t)sizeof wr));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                      runp(TFW_CMD_ONEWIRE_READ, rd, (uint8_t)sizeof rd));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_ow_read_count());
}

/* Kills: the RESERVED term dropped from sched.c's TIMER_SET mask (line 85);
 * on the 328P table the refused pins are IN_USE only. */
static void test_timer_set_on_a_reserved_pin_is_refused_and_it_stays(void)
{
    static const uint8_t p[6] = { 3u, 1u, 0xE8u, 0x03u, 0x00u, 0x00u }; /* 1000 ms, u32 LE */

    TEST_ASSERT_EQUAL(TFW_ERR_RESERVED_PIN,
                      runp(TFW_CMD_TIMER_SET, p, (uint8_t)sizeof p));
    TEST_ASSERT_FALSE(tfw_mock_pin_is_output(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_mock_pin_level(TFW_ADDR_PIND, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_on_a_reserved_compare_pin_is_refused_and_not_driven);
    RUN_TEST(test_pwm_on_an_unreserved_compare_pin_is_driven);
    RUN_TEST(test_pin_mode_and_digital_write_on_a_reserved_pin_are_refused_and_it_stays);
    RUN_TEST(test_tone_on_a_reserved_tone_pin_is_refused_and_silent);
    RUN_TEST(test_spi_xfer_with_a_reserved_chip_select_is_refused_with_no_reply);
    RUN_TEST(test_onewire_on_a_reserved_pin_is_refused_and_never_reaches_the_bus);
    RUN_TEST(test_timer_set_on_a_reserved_pin_is_refused_and_it_stays);
    return UNITY_END();
}
