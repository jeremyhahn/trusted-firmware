/* test_caps_wire.c -- the device-capabilities command table as
 * docs/device-capabilities/api.md states it: each command's id and its
 * min/max payload, the reserved pulse-count ids, and NO_TONE's optional pin.
 *
 * The ids are literals from the document, not the macros: a renumbered macro
 * must fail here, since an older host would read it as a different command. */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "board_profile.h"

/* One byte past the largest payload, so max + 1 can be offered. */
static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)    { tfw_mock_reset(); }
void tearDown(void) {}

static tfw_status_t run_n(uint8_t cmd, uint8_t n)
{
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

static tfw_status_t run(uint8_t cmd, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    return run_n(cmd, n);
}

typedef struct {
    const char *name;
    uint8_t     id;
    uint8_t     macro;
    uint8_t     min;
    uint8_t     max;
} caps_cmd_t;

/* api.md "GPIO" through "Buses", row for row. */
static const caps_cmd_t CAPS_CMDS[] = {
    { "PIN_MODE",      0x10u, TFW_CMD_PIN_MODE,      2u, 2u },
    { "DIGITAL_READ",  0x11u, TFW_CMD_DIGITAL_READ,  1u, 1u },
    { "DIGITAL_WRITE", 0x12u, TFW_CMD_DIGITAL_WRITE, 2u, 2u },
    { "TIMER_SET",     0x14u, TFW_CMD_TIMER_SET,     6u, 6u },
    { "TIMER_CANCEL",  0x15u, TFW_CMD_TIMER_CANCEL,  2u, 2u },
    { "TIMER_QUERY",   0x16u, TFW_CMD_TIMER_QUERY,   1u, 1u },
    { "ANALOG_READ",   0x20u, TFW_CMD_ANALOG_READ,   1u, 1u },
    { "ADC_REF",       0x21u, TFW_CMD_ADC_REF,       1u, 1u },
    { "PWM_WRITE",     0x22u, TFW_CMD_PWM_WRITE,     2u, 2u },
    { "TONE",          0x23u, TFW_CMD_TONE,          3u, 3u },
    { "NO_TONE",       0x24u, TFW_CMD_NO_TONE,       0u, 1u },
    { "PULSE_IN",      0x30u, TFW_CMD_PULSE_IN,      6u, 6u },
    { "PULSE_CAPTURE", 0x34u, TFW_CMD_PULSE_CAPTURE, 7u, 7u },
    { "EEPROM_READ",   0x40u, TFW_CMD_EEPROM_READ,   3u, 3u },
    { "EEPROM_WRITE",  0x41u, TFW_CMD_EEPROM_WRITE,  3u, TFW_MAX_PAYLOAD },
    { "I2C_XFER",      0x50u, TFW_CMD_I2C_XFER,      2u, TFW_MAX_PAYLOAD },
    { "SPI_XFER",      0x51u, TFW_CMD_SPI_XFER,      2u, TFW_MAX_PAYLOAD },
    { "ONEWIRE_RESET", 0x52u, TFW_CMD_ONEWIRE_RESET, 1u, 1u },
    { "ONEWIRE_WRITE", 0x53u, TFW_CMD_ONEWIRE_WRITE, 2u, TFW_MAX_PAYLOAD },
    { "ONEWIRE_READ",  0x54u, TFW_CMD_ONEWIRE_READ,  2u, 2u },
    { "I2C_CONFIG",    0x55u, TFW_CMD_I2C_CONFIG,    4u, 4u },
    { "SPI_CONFIG",    0x56u, TFW_CMD_SPI_CONFIG,    3u, 3u },
};
#define N_CAPS_CMDS (sizeof CAPS_CMDS / sizeof CAPS_CMDS[0])

static void test_every_capability_has_its_documented_id_and_is_served(void)
{
    size_t i;
    for (i = 0; i < N_CAPS_CMDS; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(CAPS_CMDS[i].id, CAPS_CMDS[i].macro, CAPS_CMDS[i].name);
        TEST_ASSERT_TRUE_MESSAGE(tfw_dispatch_knows(CAPS_CMDS[i].id), CAPS_CMDS[i].name);
    }
}

/* The dispatcher refuses a payload outside the documented bounds before the
 * handler sees it, so a short request cannot be read as a different one. */
static void test_a_payload_outside_the_documented_bounds_is_bad_argc(void)
{
    size_t i;
    memset(inbuf, 0, sizeof inbuf);
    for (i = 0; i < N_CAPS_CMDS; i++) {
        const caps_cmd_t *c = &CAPS_CMDS[i];
        if (c->min > 0u) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run_n(c->id, (uint8_t)(c->min - 1u)), c->name);
        }
        if (c->max < TFW_MAX_PAYLOAD) {
            TEST_ASSERT_EQUAL_MESSAGE(TFW_ERR_BAD_ARGC, run_n(c->id, (uint8_t)(c->max + 1u)), c->name);
        }
    }
}

/* api.md "Pulse": 0x31-0x33 are reserved with no handler and answer as an id
 * that was never allocated. */
static void test_the_reserved_pulse_count_ids_are_unknown(void)
{
    uint8_t id;
    TEST_ASSERT_EQUAL_HEX8(0x31u, TFW_CMD_PULSE_COUNT_START);
    TEST_ASSERT_EQUAL_HEX8(0x32u, TFW_CMD_PULSE_COUNT_READ);
    TEST_ASSERT_EQUAL_HEX8(0x33u, TFW_CMD_PULSE_COUNT_STOP);
    for (id = 0x31u; id <= 0x33u; id++) {
        TEST_ASSERT_FALSE(tfw_dispatch_knows(id));
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run_n(id, 0u));
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(id, (const uint8_t[]){ 2u, 1u }, 2u));
    }
}

static void start_tone(void)
{
    const uint8_t on[3] = { TFW_TONE_PIN, 0x10u, 0x27u };   /* 10000 Hz */
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_TONE, on, 3u));
    TEST_ASSERT_EQUAL_UINT16(10000u, tfw_mock_tone_hz());
}

/* NO_TONE: "pin ignored if present". With no payload it stops the tone. */
static void test_no_tone_with_no_payload_stops_the_tone(void)
{
    start_tone();
    TEST_ASSERT_EQUAL(TFW_OK, run_n(TFW_CMD_NO_TONE, 0u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
}

/* And a pin that is not the tone pin is ignored, not refused: there is one
 * tone and NO_TONE stops it. */
static void test_no_tone_ignores_the_pin_it_is_given(void)
{
    start_tone();
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_NO_TONE, (const uint8_t[]){ 3u }, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
    start_tone();
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_NO_TONE, (const uint8_t[]){ 0xFFu }, 1u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_mock_tone_hz());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_capability_has_its_documented_id_and_is_served);
    RUN_TEST(test_a_payload_outside_the_documented_bounds_is_bad_argc);
    RUN_TEST(test_the_reserved_pulse_count_ids_are_unknown);
    RUN_TEST(test_no_tone_with_no_payload_stops_the_tone);
    RUN_TEST(test_no_tone_ignores_the_pin_it_is_given);
    return UNITY_END();
}
