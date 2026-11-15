/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-License-Identifier: commercial
 *
 * The bench self-check (src/hal/esp32/hal_selfcheck.c, #48) built with
 * CONFIG_TFW_PERIPH_SELFCHECK on the PoE board's profile, over stand-in gpio
 * and LEDC drivers (test/mock/esp32/selfcheck), the real EEPROM HAL
 * (src/hal/esp32/hal_eeprom.c) on a modelled NOR partition, and the suite's
 * own other HAL and board calls. Each console line the check
 * logs is kept, so a test asserts the verdict it printed.
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

uint32_t ledc_get_duty(ledc_mode_t mode, ledc_channel_t channel)
{
    (void)mode;
    (void)channel;
    return g_duty;
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
    TEST_ASSERT_TRUE(verdict_is("hal_bus", "NOT RUN"));
    TEST_ASSERT_EQUAL_MEMORY(g_before, g_flash, sizeof g_before);
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
    RUN_TEST(test_a_high_c6_enable_fails_though_the_held_output_reads_zero);
    RUN_TEST(test_the_c6_check_leaves_the_enable_low_and_held);
    RUN_TEST(test_a_program_failure_after_the_erase_restores_the_whole_sector);
    RUN_TEST(test_a_failed_erase_restores_the_whole_sector);
    RUN_TEST(test_a_failed_readback_still_restores_the_whole_sector);
    RUN_TEST(test_a_failed_restore_is_reported_on_its_own_line);
    RUN_TEST(test_a_restore_wrong_outside_the_tested_bytes_fails);
    RUN_TEST(test_an_unreadable_sector_is_never_written);
    return UNITY_END();
}
