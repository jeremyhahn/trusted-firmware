/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-License-Identifier: commercial
 *
 * The bench self-check (src/hal/esp32/hal_selfcheck.c, #48) built with
 * CONFIG_TFW_PERIPH_SELFCHECK and the PoE board's co-processor enable, over
 * stand-in gpio and LEDC drivers (test/mock/esp32/selfcheck), a modelled
 * EEPROM and the suite's own HAL and board calls. Each console line the check
 * logs is kept, so a test asserts the verdict it printed.
 */

#include "unity.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
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

uint32_t ledc_get_duty(ledc_mode_t mode, ledc_channel_t channel)
{
    (void)mode;
    (void)channel;
    return g_duty;
}

/* ------------------------------------------------------- the EEPROM model */

static uint8_t g_ee[TFW_EEPROM_SIZE];
static unsigned g_reads, g_writes;
static unsigned g_fail_read_at;   /* 1-based call to fail, 0 for none */
static unsigned g_fail_write_at;
static unsigned g_fail_restore;   /* fail every write after the first */

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    g_reads++;
    if (g_reads == g_fail_read_at) return TFW_ERR_INTERNAL;
    memcpy(buf, &g_ee[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    g_writes++;
    if (g_fail_restore && g_writes > 1u) return TFW_ERR_INTERNAL;
    /* A failed write may still have changed the flash: it lands, then fails. */
    memcpy(&g_ee[addr], buf, len);
    if (g_writes == g_fail_write_at) return TFW_ERR_INTERNAL;
    return TFW_OK;
}

/* --------------------------------------------------- the other HAL calls */

static uint8_t g_gpio_level;

tfw_status_t tfw_board_pin(tfw_pin_t pin, tfw_pinref_t *out)
{
    (void)pin;
    memset(out, 0, sizeof *out);
    return TFW_OK;
}

tfw_status_t tfw_board_adc_channel(tfw_pin_t pin, uint8_t *out)
{
    (void)pin;
    *out = 4u;
    return TFW_OK;
}

tfw_status_t tfw_board_pwm(tfw_pin_t pin, tfw_pwmref_t *out)
{
    (void)pin;
    memset(out, 0, sizeof *out);
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    (void)p;
    (void)mode;
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
    *out = g_gpio_level;
    return TFW_OK;
}

tfw_status_t tfw_hal_adc_init(void) { return TFW_OK; }

tfw_status_t tfw_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    (void)channel;
    *out_raw = 2048u;
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    (void)p;
    g_duty = duty;
    return TFW_OK;
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
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void) { return TFW_OK; }

tfw_status_t tfw_hal_pulse_in(const tfw_pinref_t *p, uint8_t level,
                              uint32_t timeout_us, uint32_t *out_us)
{
    (void)p;
    (void)level;
    (void)timeout_us;
    *out_us = 0u;
    return TFW_ERR_TIMEOUT;
}

void tfw_hal_delay_us(uint16_t us) { (void)us; }

/* ------------------------------------------------------------------ tests */

#define EE_AT (TFW_EEPROM_SIZE - 16u)

static uint8_t g_before[16];

void setUp(void)
{
    g_log[0] = '\0';
    /* As tfw_esp_board_init() leaves the enable: an output driven low and
     * held, its input buffer off. */
    g_pad_ext = 0;
    g_pad_input = 0;
    g_pad_held = 1;
    g_pad_out = 0u;
    g_reads = g_writes = 0u;
    g_fail_read_at = g_fail_write_at = g_fail_restore = 0u;
    for (unsigned i = 0u; i < sizeof g_ee; i++) g_ee[i] = (uint8_t)(i * 7u);
    memcpy(g_before, &g_ee[EE_AT], sizeof g_before);
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
    TEST_ASSERT_TRUE(verdict_is("hal_bus", "NOT RUN"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, &g_ee[EE_AT], sizeof g_before);
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

static void test_a_failed_readback_still_restores_the_old_bytes(void)
{
    g_fail_read_at = 2u;   /* 1: the save, 2: the pattern's readback */
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, &g_ee[EE_AT], sizeof g_before);
}

static void test_a_failed_pattern_write_that_changed_flash_is_restored(void)
{
    g_fail_write_at = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "PASS"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, &g_ee[EE_AT], sizeof g_before);
}

static void test_a_failed_restore_is_reported_on_its_own_line(void)
{
    g_fail_restore = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "PASS"));
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom_restore", "FAIL"));
}

static void test_an_unreadable_store_is_never_written(void)
{
    g_fail_read_at = 1u;
    tfw_esp_selfcheck();
    TEST_ASSERT_TRUE(verdict_is("hal_eeprom", "FAIL"));
    TEST_ASSERT_EQUAL_UINT(0u, g_writes);
    TEST_ASSERT_NULL(line_of("hal_eeprom_restore"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_check_passes_on_a_good_board);
    RUN_TEST(test_a_high_c6_enable_fails_though_the_held_output_reads_zero);
    RUN_TEST(test_the_c6_check_leaves_the_enable_low_and_held);
    RUN_TEST(test_a_failed_readback_still_restores_the_old_bytes);
    RUN_TEST(test_a_failed_pattern_write_that_changed_flash_is_restored);
    RUN_TEST(test_a_failed_restore_is_reported_on_its_own_line);
    RUN_TEST(test_an_unreadable_store_is_never_written);
    return UNITY_END();
}
