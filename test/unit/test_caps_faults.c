/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The capability handlers when the hardware says no, and the arity table that
 * guards every one of them.
 *
 * A bus that times out, an EEPROM that will not read and a pin the HAL cannot
 * drive must each reach the host as the HAL's own status with no reply bytes:
 * a host that got a status and a half-filled buffer would read the half as
 * data. The ESP32 HAL's gpio write can fail; the AVR one cannot, so these paths
 * are reachable only on the mock until a second board runs the suite.
 */

#include <string.h>
#include "unity.h"
#include "sched.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/board.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

#define PIN       2u
#define PIN_PORT  TFW_ADDR_PIND
#define PIN_BIT   2u
#define OW_PIN    9u

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); tfw_sched_init(); }
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t timer_set(tfw_pin_t pin, uint8_t level, uint32_t ms)
{
    uint8_t p[6] = { pin, level,
                     (uint8_t)(ms), (uint8_t)(ms >> 8),
                     (uint8_t)(ms >> 16), (uint8_t)(ms >> 24) };
    return run(TFW_CMD_TIMER_SET, p, 6u);
}

static uint8_t level_now(void) { return tfw_mock_pin_level(PIN_PORT, PIN_BIT); }

/* --- buses ---------------------------------------------------------------- */

static void test_an_i2c_bus_error_reaches_the_host_with_no_bytes(void)
{
    tfw_mock_bus_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_I2C_XFER, (const uint8_t[]){ 0x48u, 2u, 0x00u }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

static void test_an_spi_bus_error_reaches_the_host_with_no_bytes(void)
{
    tfw_mock_bus_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_SPI_XFER, (const uint8_t[]){ 10u, 0xAAu, 0xBBu }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

static void test_a_onewire_bus_error_reaches_the_host_with_no_bytes(void)
{
    tfw_mock_bus_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_ONEWIRE_RESET, (const uint8_t[]){ OW_PIN }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_ONEWIRE_WRITE, (const uint8_t[]){ OW_PIN, 0xCCu, 0xBEu }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_ONEWIRE_READ, (const uint8_t[]){ OW_PIN, 2u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* --- EEPROM ---------------------------------------------------------------- */

static void test_an_eeprom_read_error_reaches_the_host_with_no_bytes(void)
{
    tfw_mock_eeprom_read_fails(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_EEPROM_READ, (const uint8_t[]){ 0x00u, 0x01u, 4u }, 3u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* --- GPIO and the scheduler ------------------------------------------------ */

static void test_a_pin_the_hal_cannot_read_or_drive_reports_it(void)
{
    tfw_mock_gpio_io_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_DIGITAL_READ, (const uint8_t[]){ PIN }, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_DIGITAL_WRITE, (const uint8_t[]){ PIN, 1u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
}

/* A timer whose pin could not be driven is not armed: no slot, no index. */
static void test_a_timer_on_a_pin_the_hal_cannot_drive_is_not_armed(void)
{
    tfw_mock_gpio_io_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, timer_set(PIN, 1u, 5000u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
    TEST_ASSERT_EQUAL_UINT8(0u, w.pos);
}

/* A failed re-arm leaves the running timer exactly as it was: the pin still
 * reverts at the first deadline, with no host present. */
static void test_a_failed_rearm_keeps_the_first_deadline(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    tfw_mock_gpio_io_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT, timer_set(PIN, 1u, 60000u));
    tfw_mock_gpio_io_fails(TFW_OK);

    tfw_mock_advance(5000u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
}

/* A cancel that asked for the final level and could not drive it has not
 * cancelled anything: the host hears the HAL's status, and the timer stays
 * armed so the pin still reverts on time with no host present. Answering OK
 * and dropping the slot would leave the pin held for good. */
static void test_a_cancel_that_cannot_apply_the_final_level_keeps_the_timer(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, timer_set(PIN, 1u, 5000u));
    tfw_mock_gpio_io_fails(TFW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(TFW_ERR_TIMEOUT,
        run(TFW_CMD_TIMER_CANCEL, (const uint8_t[]){ PIN, 1u }, 2u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_sched_active());
    tfw_mock_gpio_io_fails(TFW_OK);

    tfw_mock_advance(5000u);
    tfw_sched_poll();
    TEST_ASSERT_EQUAL_UINT8(0u, level_now());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_sched_active());
}

/* --- arity ------------------------------------------------------------------ */

/* Every capability command's payload bounds, as literals. The dispatcher reads
 * them from commands.def; a test that read them from there too would move with
 * any edit to it. */
static const struct { uint8_t cmd, min, max; } arity[] = {
    { TFW_CMD_PIN_MODE,      2u, 2u },
    { TFW_CMD_DIGITAL_READ,  1u, 1u },
    { TFW_CMD_DIGITAL_WRITE, 2u, 2u },
    { TFW_CMD_TIMER_SET,     6u, 6u },
    { TFW_CMD_TIMER_CANCEL,  2u, 2u },
    { TFW_CMD_TIMER_QUERY,   1u, 1u },
    { TFW_CMD_ANALOG_READ,   1u, 1u },
    { TFW_CMD_ADC_REF,       1u, 1u },
    { TFW_CMD_PWM_WRITE,     2u, 2u },
    { TFW_CMD_TONE,          3u, 3u },
    { TFW_CMD_NO_TONE,       0u, 1u },
    { TFW_CMD_PULSE_IN,      6u, 6u },
    { TFW_CMD_PULSE_CAPTURE, 7u, 7u },
    { TFW_CMD_EEPROM_READ,   3u, 3u },
    { TFW_CMD_EEPROM_WRITE,  3u, TFW_MAX_PAYLOAD },
    { TFW_CMD_I2C_XFER,      2u, TFW_MAX_PAYLOAD },
    { TFW_CMD_I2C_CONFIG,    4u, 4u },
    { TFW_CMD_SPI_XFER,      2u, TFW_MAX_PAYLOAD },
    { TFW_CMD_SPI_CONFIG,    3u, 3u },
    { TFW_CMD_ONEWIRE_RESET, 1u, 1u },
    { TFW_CMD_ONEWIRE_WRITE, 2u, TFW_MAX_PAYLOAD },
    { TFW_CMD_ONEWIRE_READ,  2u, 2u },
};

/* A payload that would drive pin 2 high for 16 ms if any handler parsed it:
 * the refusal is proved by the pin, not only by the status. */
static void refused(uint8_t cmd, uint8_t n)
{
    uint8_t p[TFW_MAX_PAYLOAD];

    memset(p, 0, sizeof p);
    p[0] = PIN; p[1] = 1u; p[2] = 16u;
    TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run(cmd, p, n), "status");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, w.pos, "reply bytes");
    TEST_ASSERT_FALSE_MESSAGE(tfw_mock_pin_is_output(PIN_PORT, PIN_BIT), "pin driven");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_sched_active(), "timer armed");
}

static void test_every_capability_refuses_a_payload_outside_its_bounds(void)
{
    size_t i;

    for (i = 0u; i < sizeof arity / sizeof arity[0]; i++) {
        if (arity[i].min > 0u) {
            refused(arity[i].cmd, (uint8_t)(arity[i].min - 1u));
        }
        if (arity[i].max < TFW_MAX_PAYLOAD) {
            refused(arity[i].cmd, (uint8_t)(arity[i].max + 1u));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_i2c_bus_error_reaches_the_host_with_no_bytes);
    RUN_TEST(test_an_spi_bus_error_reaches_the_host_with_no_bytes);
    RUN_TEST(test_a_onewire_bus_error_reaches_the_host_with_no_bytes);
    RUN_TEST(test_an_eeprom_read_error_reaches_the_host_with_no_bytes);
    RUN_TEST(test_a_pin_the_hal_cannot_read_or_drive_reports_it);
    RUN_TEST(test_a_timer_on_a_pin_the_hal_cannot_drive_is_not_armed);
    RUN_TEST(test_a_failed_rearm_keeps_the_first_deadline);
    RUN_TEST(test_a_cancel_that_cannot_apply_the_final_level_keeps_the_timer);
    RUN_TEST(test_every_capability_refuses_a_payload_outside_its_bounds);
    return UNITY_END();
}
