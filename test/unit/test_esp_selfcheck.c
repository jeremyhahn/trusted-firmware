/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-License-Identifier: commercial
 *
 * The bench self-check (src/hal/esp32/hal_selfcheck.c, #48) built with
 * CONFIG_TFW_PERIPH_SELFCHECK on the PoE board's profile, over stand-in gpio
 * and LEDC drivers (test/mock/esp32/selfcheck), the real EEPROM HAL
 * (src/hal/esp32/hal_eeprom.c) on a modelled NOR partition, and the suite's
 * own other HAL and board calls. Each console line the check
 * logs is kept, so a test asserts the verdict it printed. The board lookups
 * answer with the PoE board's own rows (boards/esp32p4-poe/board.c) and
 * record which pin each check asked for.
 */

#include "unity.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_partition.h"
#include "hal_esp32.h"
#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/hal.h"

/* ----------------------------------------------------------- console lines */

static char g_log[4096];

void mock_esp_log(char level, const char *tag, const char *fmt, ...)
{
    size_t n = strlen(g_log);
    va_list ap;

    (void)level;
    (void)tag;
    va_start(ap, fmt);
    (void)vsnprintf(g_log + n, sizeof g_log - n, fmt, ap);
    va_end(ap);
    n = strlen(g_log);
    if (n + 1u < sizeof g_log) {
        g_log[n] = '\n';
        g_log[n + 1u] = '\0';
    }
}

/* The line that starts with "<name> ", or NULL. */
static const char *line_of(const char *name)
{
    size_t len = strlen(name);
    const char *p = g_log;

    while (*p != '\0') {
        if (strncmp(p, name, len) == 0 && p[len] == ' ') return p;
        p = strchr(p, '\n');
        if (p == NULL) return NULL;
        p++;
    }
    return NULL;
}

static int verdict_is(const char *name, const char *verdict)
{
    const char *l = line_of(name);
    size_t len = strlen(name);

    return l != NULL && strncmp(l + len + 1u, verdict, strlen(verdict)) == 0;
}

/* ------------------------------------------------------- the modelled pad */

static int g_pad_ext;        /* the level the pad would read with its input on */
static int g_pad_input;      /* the input buffer */
static int g_pad_held;
static uint32_t g_pad_out = 1u;

esp_err_t gpio_hold_en(gpio_num_t n)
{
    TEST_ASSERT_EQUAL_INT(TFW_ESP_COPROC_EN_GPIO, n);
    g_pad_held = 1;
    return ESP_OK;
}

esp_err_t gpio_hold_dis(gpio_num_t n)
{
    TEST_ASSERT_EQUAL_INT(TFW_ESP_COPROC_EN_GPIO, n);
    g_pad_held = 0;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t n, uint32_t level)
{
    TEST_ASSERT_EQUAL_INT(TFW_ESP_COPROC_EN_GPIO, n);
    if (!g_pad_held) g_pad_out = level;
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t n, gpio_mode_t mode)
{
    TEST_ASSERT_EQUAL_INT(TFW_ESP_COPROC_EN_GPIO, n);
    if (!g_pad_held) g_pad_input = (mode == GPIO_MODE_INPUT || mode == GPIO_MODE_INPUT_OUTPUT);
    return ESP_OK;
}

int gpio_get_level(gpio_num_t n)
{
    TEST_ASSERT_EQUAL_INT(TFW_ESP_COPROC_EN_GPIO, n);
    return g_pad_input ? g_pad_ext : 0;
}

/* --------------------------------------------------------------- the LEDC */

static uint32_t g_duty;
static int g_ledc_as;   /* the duty LEDC reads back, -1: the one written */

uint32_t ledc_get_duty(ledc_mode_t mode, ledc_channel_t channel)
{
    (void)mode;
    (void)channel;
    return g_ledc_as < 0 ? g_duty : (uint32_t)g_ledc_as;
}

/* ------------------------------------------- the tfw_eeprom partition */

/* The board's tfw_eeprom partition (boards/esp32p4-poe/partitions.csv): one
 * 4 KB NOR sector under the real src/hal/esp32/hal_eeprom.c. An erase sets
 * every byte to 0xFF and a program can only clear bits, as on the chip. Each
 * fault is the 1-based call it hits, 0 for none. */
#define PART_SIZE 0x1000u

static const esp_partition_t g_part = {
    .type = ESP_PARTITION_TYPE_DATA, .subtype = 0x42, .address = 0x13000u,
    .size = PART_SIZE, .erase_size = PART_SIZE, .label = "tfw_eeprom",
};
static uint8_t g_flash[PART_SIZE];
static unsigned g_reads, g_erases, g_programs;
static unsigned g_fail_read_at;
static unsigned g_fail_erase_at;     /* the sector is erased, then it fails */
static unsigned g_fail_program_at;   /* half the range is programmed, then it fails */
static unsigned g_stuck_program_at;  /* reports OK, but byte 0 stays 0x00 */

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    (void)type;
    (void)subtype;
    return strcmp(label, g_part.label) == 0 ? &g_part : NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t size)
{
    TEST_ASSERT_EQUAL_PTR(&g_part, p);
    TEST_ASSERT_TRUE(off + size <= PART_SIZE);
    if (++g_reads == g_fail_read_at) return ESP_FAIL;
    memcpy(dst, &g_flash[off], size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t size)
{
    TEST_ASSERT_EQUAL_PTR(&g_part, p);
    TEST_ASSERT_TRUE(off + size <= PART_SIZE);
    memset(&g_flash[off], 0xff, size);
    return ++g_erases == g_fail_erase_at ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t size)
{
    const uint8_t *b = src;
    size_t n = size;

    TEST_ASSERT_EQUAL_PTR(&g_part, p);
    TEST_ASSERT_TRUE(off + size <= PART_SIZE);
    g_programs++;
    if (g_programs == g_fail_program_at) n = size / 2u;
    for (size_t i = 0u; i < n; i++) g_flash[off + i] &= b[i];
    if (g_programs == g_stuck_program_at) g_flash[off] = 0x00u;
    return g_programs == g_fail_program_at ? ESP_FAIL : ESP_OK;
}

/* --------------------------------------------------- the other HAL calls */

static uint8_t g_gpio_level;
static tfw_pin_mode_t g_gpio_mode;

/* The peripherals as a good board answers unless a test says otherwise: the
 * status each call returns, and a reading forced wrong (-1: the true one). */
static tfw_status_t g_gpio_read_st;
static int g_gpio_read_as;
static tfw_status_t g_adc_st;
static uint16_t g_adc_raw;
static tfw_status_t g_pwm_st;
static tfw_status_t g_tone_start_st;
static tfw_status_t g_tone_stop_st;
static tfw_status_t g_pulse_st;
static uint32_t g_pulse_us;

/* The buses as the bench leaves them -- no device on any -- unless a test says
 * otherwise: what the I2C probe returns, the SPI transfer's status, the
 * 1-Wire presence and the byte the line reads; and what the check asked for. */
static tfw_status_t g_i2c_st;
static tfw_status_t g_spi_st;
static uint8_t g_ow_presence;
static uint8_t g_ow_byte;
static int g_i2c_addr;
static int g_i2c_probe;
static int g_spi_cs_at_xfer;
static int g_spi_xfers;

/* The PoE board's own pin rows, copied from boards/esp32p4-poe/board.c lines
 * 21-37: the file cannot be #included here, since the dependency file then
 * puts it in the link line of mk/host.mk's rule and its tables link twice.
 * Then the pins and ADC channel the checks asked for, in order. */
static const tfw_pin_desc_t g_poe_pins[TFW_PIN_COUNT] = {
/*  0  GPIO2   LEDC0      */ { TFW_PB(0u,  2u), TFW_ADC_NONE, 0u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  1  GPIO3   LEDC1      */ { TFW_PB(0u,  3u), TFW_ADC_NONE, 1u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  2  GPIO4   LEDC2      */ { TFW_PB(0u,  4u), TFW_ADC_NONE, 2u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  3  GPIO5   LEDC3      */ { TFW_PB(0u,  5u), TFW_ADC_NONE, 3u,           TFW_PIN_DIGITAL | TFW_PIN_PWM },
/*  4  GPIO6   C6 IO2     */ { TFW_PB(0u,  6u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  5  GPIO7   I2C SDA    */ { TFW_PB(0u,  7u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  6  GPIO8   I2C SCL    */ { TFW_PB(0u,  8u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/*  7  GPIO20  ADC1_CH4   */ { TFW_PB(0u, 20u), 4u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/*  8  GPIO21  ADC1_CH5   */ { TFW_PB(0u, 21u), 5u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/*  9  GPIO22  ADC1_CH6   */ { TFW_PB(0u, 22u), 6u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 10  GPIO23  ADC1_CH7   */ { TFW_PB(0u, 23u), 7u,           TFW_PWM_NONE, TFW_PIN_DIGITAL | TFW_PIN_ADC },
/* 11  GPIO32             */ { TFW_PB(1u,  0u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 12  GPIO33             */ { TFW_PB(1u,  1u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 13  GPIO46  SPI MOSI   */ { TFW_PB(1u, 14u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 14  GPIO47  SPI MISO   */ { TFW_PB(1u, 15u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 15  GPIO48  SPI SCK    */ { TFW_PB(1u, 16u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
/* 16  GPIO53  PA_CTRL    */ { TFW_PB(1u, 21u), TFW_ADC_NONE, TFW_PWM_NONE, TFW_PIN_DIGITAL },
};

static tfw_pin_t g_pins_asked[8];
static unsigned g_pins_n;
static int g_adc_pin_asked, g_pwm_pin_asked, g_adc_ch_read;

tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out)
{
    if (g_pins_n < sizeof g_pins_asked / sizeof g_pins_asked[0]) g_pins_asked[g_pins_n] = pin;
    g_pins_n++;
    memset(out, 0, sizeof *out);
    return TFW_OK;
}

tfw_status_t tfw_board_adc_channel(tfw_pin_t pin, uint8_t *out)
{
    g_adc_pin_asked = pin;
    if (pin >= TFW_PIN_COUNT) return TFW_ERR_INTERNAL;
    *out = g_poe_pins[pin].adc_ch;
    return TFW_OK;
}

tfw_status_t tfw_board_pwm(tfw_pin_t pin, tfw_pwmref_t *out)
{
    g_pwm_pin_asked = pin;
    memset(out, 0, sizeof *out);
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    (void)p;
    g_gpio_mode = mode;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    (void)p;
    g_gpio_level = level;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    (void)p;
    *out = g_gpio_read_as < 0 ? g_gpio_level : (uint8_t)g_gpio_read_as;
    return g_gpio_read_st;
}

tfw_status_t tfw_hal_adc_init(void) { return TFW_OK; }

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    g_adc_ch_read = channel;
    *out_raw = g_adc_raw;
    return g_adc_st;
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    (void)p;
    g_duty = duty;
    return g_pwm_st;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    (void)p;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    (void)p;
    (void)hz;
    return g_tone_start_st;
}

tfw_status_t tfw_hal_tone_stop(void) { return g_tone_stop_st; }

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                              uint32_t timeout_us, uint32_t *out_us)
{
    (void)p;
    (void)level;
    (void)timeout_us;
    *out_us = g_pulse_us;
    return g_pulse_st;
}

void tfw_hal_delay_us(uint16_t us) { (void)us; }

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    (void)hz;
    return TFW_OK;
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                             uint8_t *rd, uint8_t rlen)
{
    g_i2c_addr = addr7;
    g_i2c_probe = wr == NULL && wlen == 0u && rd == NULL && rlen == 0u;
    return g_i2c_st;
}

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    (void)mode;
    (void)clkdiv;
    (void)lsb_first;
    return TFW_OK;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                             uint8_t *rd, uint8_t len)
{
    (void)cs;
    (void)wr;
    memset(rd, 0xff, len);
    g_spi_cs_at_xfer = g_gpio_mode == TFW_PIN_OUTPUT && g_gpio_level == 1u;
    g_spi_xfers++;
    return g_spi_st;
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    (void)p;
    *out_presence = g_ow_presence;
    return TFW_OK;
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    (void)p;
    memset(buf, g_ow_byte, len);
    return TFW_OK;
}

/* ------------------------------------------------------------------ tests */

static uint8_t g_before[PART_SIZE];

void setUp(void)
{
    g_log[0] = '\0';
    /* As tfw_esp_board_init() leaves the enable: an output driven low and
     * held, its input buffer off. */
    g_pad_ext = 0;
    g_pad_input = 0;
    g_pad_held = 1;
    g_pad_out = 0u;
    g_reads = g_erases = g_programs = 0u;
    g_fail_read_at = g_fail_erase_at = g_fail_program_at = g_stuck_program_at = 0u;
    g_gpio_read_st = TFW_OK;
    g_gpio_read_as = -1;
    g_adc_st = TFW_OK;
    g_adc_raw = 2048u;
    g_pwm_st = TFW_OK;
    g_ledc_as = -1;
    g_tone_start_st = g_tone_stop_st = TFW_OK;
    g_pulse_st = TFW_ERR_TIMEOUT;
    g_pulse_us = 0u;
    g_i2c_st = TFW_ERR_NACK;
    g_spi_st = TFW_OK;
    g_ow_presence = 0u;
    g_ow_byte = 0xffu;
    g_i2c_addr = -1;
    g_i2c_probe = 0;
    g_spi_cs_at_xfer = 0;
    g_spi_xfers = 0;
    g_pins_n = 0u;
    g_adc_pin_asked = g_pwm_pin_asked = g_adc_ch_read = -1;
    for (unsigned i = 0u; i < sizeof g_flash; i++) g_flash[i] = (uint8_t)(i * 7u + 3u);
    memcpy(g_before, g_flash, sizeof g_before);
}

void tearDown(void) {}

static void test_every_check_passes_on_a_good_board(void)
{
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("c6_enable", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_gpio", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_adc", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_pwm", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_tone", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_pulse_rmt", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_bus_i2c", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_bus_spi", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_bus_1wire", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
}

/* gpio, tone, pulse, SPI chip select, 1-Wire: each on the header pin its
 * comment in hal_selfcheck.c names, none claimed by the board or the build;
 * the ADC reads the channel of its pin's row, and PWM runs LEDC channel 0. */
static void test_each_check_uses_the_free_header_pin_it_names(void)
{
    static const tfw_pin_t want[] = { 11u, TFW_TONE_PIN, 12u, 11u, 12u };

    tfw_esp_selfcheck();
    TEST_ASSERT_EQUAL_UINT(sizeof want / sizeof want[0], g_pins_n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, g_pins_asked, sizeof want / sizeof want[0]);
    TEST_ASSERT_EQUAL_INT(7, g_adc_pin_asked);
    TEST_ASSERT_EQUAL_INT(4, g_adc_ch_read);
    TEST_ASSERT_EQUAL_INT(0, g_pwm_pin_asked);
    for (unsigned i = 0u; i < g_pins_n; i++) {
        TEST_ASSERT_TRUE(g_pins_asked[i] < TFW_PIN_COUNT);
        TEST_ASSERT_EQUAL_HEX8(0u, g_poe_pins[g_pins_asked[i]].flags &
                                       (TFW_PIN_RESERVED | TFW_PIN_IN_USE));
    }
    TEST_ASSERT_EQUAL_HEX8(0u, g_poe_pins[g_adc_pin_asked].flags &
                                   (TFW_PIN_RESERVED | TFW_PIN_IN_USE));
    TEST_ASSERT_TRUE(g_poe_pins[g_adc_pin_asked].flags & TFW_PIN_ADC);
    TEST_ASSERT_EQUAL_HEX8(0u, g_poe_pins[g_pwm_pin_asked].flags &
                                   (TFW_PIN_RESERVED | TFW_PIN_IN_USE));
    TEST_ASSERT_TRUE(g_poe_pins[g_pwm_pin_asked].flags & TFW_PIN_PWM);
    TEST_ASSERT_EQUAL_UINT8(0u, g_poe_pins[g_pwm_pin_asked].pwm_idx);
}

static void test_the_i2c_check_probes_the_empty_address(void)
{
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(g_i2c_probe);
    TEST_ASSERT_EQUAL_INT(0x08, g_i2c_addr);
}

static void test_an_i2c_ack_at_the_empty_address_fails(void)
{
    g_i2c_st = TFW_OK;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_i2c", "FAIL"));
}

static void test_an_i2c_bus_held_low_fails(void)
{
    g_i2c_st = TFW_ERR_TIMEOUT;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_i2c", "FAIL"));
}

static void test_the_spi_transfer_runs_with_the_chip_select_high_then_released(void)
{
    tfw_esp_selfcheck();
    TEST_ASSERT_EQUAL_INT(1, g_spi_xfers);
    TEST_ASSERT_TRUE(g_spi_cs_at_xfer);
    TEST_ASSERT_EQUAL_INT(TFW_PIN_INPUT, g_gpio_mode);
}

static void test_a_failed_spi_transfer_fails_and_still_releases_the_chip_select(void)
{
    g_spi_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_spi", "FAIL"));
    TEST_ASSERT_EQUAL_INT(TFW_PIN_INPUT, g_gpio_mode);
}

static void test_a_presence_pulse_on_the_free_pin_fails(void)
{
    g_ow_presence = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_1wire", "FAIL"));
}

static void test_a_1wire_line_that_reads_low_fails(void)
{
    g_ow_byte = 0x00u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_1wire", "FAIL"));
}

/* hal_bus.c returns TFW_ERR_INTERNAL for a probe the driver failed (ESP_FAIL,
 * ESP_ERR_INVALID_ARG; test_esp_bus_mutate pins it): not an unanswered
 * address, so not a PASS. */
static void test_an_i2c_driver_error_fails(void)
{
    g_i2c_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_bus_i2c", "FAIL"));
}

static void test_a_gpio_readback_that_differs_fails(void)
{
    g_gpio_read_as = 0;     /* stuck low: the high read is wrong */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_gpio", "FAIL"));

    setUp();
    g_gpio_read_as = 1;     /* stuck high: the low read is wrong */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_gpio", "FAIL"));

    setUp();
    g_gpio_read_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_gpio", "FAIL"));
}

static void test_an_adc_value_out_of_range_fails(void)
{
    g_adc_raw = 4095u;      /* the 12-bit top is in range */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_adc", "PASS"));

    setUp();
    g_adc_raw = 4096u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_adc", "FAIL"));

    setUp();
    g_adc_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_adc", "FAIL"));
}

static void test_a_wrong_pwm_duty_fails(void)
{
    g_ledc_as = 127;        /* neither 128 nor 0 */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_pwm", "FAIL"));

    setUp();
    g_ledc_as = 128;        /* right at 128, never back to 0 */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_pwm", "FAIL"));

    setUp();
    g_pwm_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_pwm", "FAIL"));
}

static void test_a_tone_that_fails_to_start_or_stop_fails(void)
{
    g_tone_start_st = TFW_ERR_BUSY;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_tone", "FAIL"));

    setUp();
    g_tone_stop_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_tone", "FAIL"));
}

/* Nothing drives the pulled-up pin, so only a timeout passes: a measured
 * pulse or any other status fails. */
static void test_an_unexpected_pulse_result_fails(void)
{
    g_pulse_st = TFW_OK;
    g_pulse_us = 500u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_pulse_rmt", "FAIL"));

    setUp();
    g_pulse_st = TFW_ERR_INTERNAL;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_pulse_rmt", "FAIL"));
}

static void test_a_high_c6_enable_fails_though_the_held_output_reads_zero(void)
{
    /* With the input buffer off the pad reads 0 whatever its level; the
     * check must read the pad itself and see the 1. */
    g_pad_ext = 1;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("c6_enable", "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(line_of("c6_enable"), "pad level 1"));
}

static void test_the_c6_check_leaves_the_enable_low_and_held(void)
{
    tfw_esp_selfcheck();
    TEST_ASSERT_EQUAL_INT(1, g_pad_held);
    TEST_ASSERT_EQUAL_UINT32(0u, g_pad_out);
}

/* The calls the check makes, in order, under the real hal_eeprom.c:
 *   reads:    1 the save, 2 the pattern write's sector, 3 the pattern's
 *             readback, 4 the restore's sector, 5 the restore's readback
 *   erases:   1 the pattern write, 2 the restore
 *   programs: 1 the pattern write, 2 the restore */

static void test_a_program_failure_after_the_erase_restores_the_whole_sector(void)
{
    /* The sector is erased, then half programmed: bytes far outside the 16
     * the check tests are lost too, and the restore must put all back. */
    g_fail_program_at = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
}

static void test_a_failed_erase_restores_the_whole_sector(void)
{
    g_fail_erase_at = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
}

static void test_a_failed_readback_still_restores_the_whole_sector(void)
{
    g_fail_read_at = 3u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
}

static void test_a_failed_restore_is_reported_on_its_own_line(void)
{
    g_fail_program_at = 2u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "FAIL"));
}

static void test_a_restore_wrong_outside_the_tested_bytes_fails(void)
{
    /* The restore reports OK but the sector's first byte did not take: the
     * check compares the whole sector, not only the 16 bytes it tested. */
    g_stuck_program_at = 2u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "FAIL"));
    TEST_ASSERT_NOT_NULL(strstr(line_of("hal_eeprom_restore"), "sector restored 0"));
}

static void test_an_unreadable_sector_is_never_written(void)
{
    g_fail_read_at = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_EQUAL_UINT(0u, g_erases);
    TEST_ASSERT_EQUAL_UINT(0u, g_programs);
    TEST_ASSERT_NULL(line_of("hal_eeprom_restore"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_check_passes_on_a_good_board);
    RUN_TEST(test_each_check_uses_the_free_header_pin_it_names);
    RUN_TEST(test_a_high_c6_enable_fails_though_the_held_output_reads_zero);
    RUN_TEST(test_the_c6_check_leaves_the_enable_low_and_held);
    RUN_TEST(test_a_program_failure_after_the_erase_restores_the_whole_sector);
    RUN_TEST(test_a_failed_erase_restores_the_whole_sector);
    RUN_TEST(test_a_failed_readback_still_restores_the_whole_sector);
    RUN_TEST(test_a_failed_restore_is_reported_on_its_own_line);
    RUN_TEST(test_a_restore_wrong_outside_the_tested_bytes_fails);
    RUN_TEST(test_an_unreadable_sector_is_never_written);
    RUN_TEST(test_the_i2c_check_probes_the_empty_address);
    RUN_TEST(test_an_i2c_ack_at_the_empty_address_fails);
    RUN_TEST(test_an_i2c_bus_held_low_fails);
    RUN_TEST(test_the_spi_transfer_runs_with_the_chip_select_high_then_released);
    RUN_TEST(test_a_failed_spi_transfer_fails_and_still_releases_the_chip_select);
    RUN_TEST(test_a_presence_pulse_on_the_free_pin_fails);
    RUN_TEST(test_a_1wire_line_that_reads_low_fails);
    RUN_TEST(test_an_i2c_driver_error_fails);
    RUN_TEST(test_a_gpio_readback_that_differs_fails);
    RUN_TEST(test_an_adc_value_out_of_range_fails);
    RUN_TEST(test_a_wrong_pwm_duty_fails);
    RUN_TEST(test_a_tone_that_fails_to_start_or_stop_fails);
    RUN_TEST(test_an_unexpected_pulse_result_fails);
    return UNITY_END();
}
