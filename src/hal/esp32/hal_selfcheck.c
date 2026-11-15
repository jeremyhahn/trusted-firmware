/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-License-Identifier: commercial
 *
 * The bench self-check of the ESP32 HAL's pin peripherals (#48).
 *
 * The peripheral commands are not on the Ethernet allow-list
 * (src/proto/link_eth.c) and the P4's wire is TCP only, so a bench build that
 * sets CONFIG_TFW_PERIPH_SELFCHECK (mk/arch/esp32/main/Kconfig.projbuild)
 * runs them here, once, from tfw_hal_init(), before the wire starts. Each
 * check prints one "tfw-selfcheck: <name> PASS|FAIL ..." line on the console.
 * Only pins the board leaves unconnected are used, and nothing needs wiring;
 * the buses (hal_bus.c) do, so they are not here.
 *
 * Off -- the default, and main's image -- the function is empty.
 */

#include "hal_esp32.h"

#if CONFIG_TFW_PERIPH_SELFCHECK

#include "tfw/board.h"
#include "tfw/hal.h"
#include "board_profile.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "tfw-selfcheck";

/* Logical pins of boards/esp32p4-poe/board.c, all on the 40-pin header and
 * left unconnected on the bench. */
#define SC_GPIO_PIN   11u   /* GPIO32 */
#define SC_ADC_PIN     7u   /* GPIO20, ADC1 channel 4 */
#define SC_PWM_PIN     0u   /* GPIO2, LEDC channel 0 */
#define SC_PULSE_PIN  12u   /* GPIO33 */
#define SC_EEPROM_AT  (TFW_EEPROM_SIZE - 16u)  /* the store's last 16 bytes */

static void report(const char *name, int pass, const char *fmt, int a, int b)
{
    char detail[64];

    (void)snprintf(detail, sizeof detail, fmt, a, b);
    ESP_LOGI(TAG, "%s %s %s", name, pass ? "PASS" : "FAIL", detail);
}

static void check_gpio(void)
{
    tfw_pinref_t p;
    uint8_t hi = 0xffu, lo = 0xffu;
    tfw_status_t st = tfw_board_pin(SC_GPIO_PIN, &p);

    if (st == TFW_OK) st = tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT);
    if (st == TFW_OK) st = tfw_hal_gpio_write(&p, 1u);
    if (st == TFW_OK) st = tfw_hal_gpio_read(&p, &hi);
    if (st == TFW_OK) st = tfw_hal_gpio_write(&p, 0u);
    if (st == TFW_OK) st = tfw_hal_gpio_read(&p, &lo);
    if (st == TFW_OK) st = tfw_hal_gpio_mode(&p, TFW_PIN_INPUT);
    report("hal_gpio", st == TFW_OK && hi == 1u && lo == 0u,
           "high read %d, low read %d", hi, lo);
}

static void check_adc(void)
{
    uint8_t ch = 0xffu;
    uint16_t raw = 0xffffu;
    tfw_status_t st = tfw_board_adc_channel(SC_ADC_PIN, &ch);

    if (st == TFW_OK) st = tfw_hal_adc_init();
    if (st == TFW_OK) st = tfw_hal_adc_read(ch, &raw);
    report("hal_adc", st == TFW_OK && raw <= 4095u,
           "status %d, raw %d", (int)st, raw);
}

static void check_pwm(void)
{
    tfw_pwmref_t p;
    uint32_t on = 0u, off = 1u;
    tfw_status_t st = tfw_board_pwm(SC_PWM_PIN, &p);

    if (st == TFW_OK) st = tfw_hal_pwm_write(&p, 128u);
    if (st == TFW_OK) on = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (st == TFW_OK) st = tfw_hal_pwm_write(&p, 0u);
    if (st == TFW_OK) off = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (st == TFW_OK) st = tfw_hal_pwm_stop(&p);
    report("hal_pwm", st == TFW_OK && on > 0u && off == 0u,
           "LEDC duty at 128: %d, at 0: %d", (int)on, (int)off);
}

static void check_tone(void)
{
    tfw_pinref_t p;
    tfw_status_t st = tfw_board_pin(TFW_TONE_PIN, &p);
    tfw_status_t stop = TFW_ERR_INTERNAL;

    if (st == TFW_OK) st = tfw_hal_tone_start(&p, 1000u);
    if (st == TFW_OK) tfw_hal_delay_us(20000u);
    if (st == TFW_OK) stop = tfw_hal_tone_stop();
    report("hal_tone", st == TFW_OK && stop == TFW_OK,
           "start %d, stop %d", (int)st, (int)stop);
}

static void check_pulse(void)
{
    tfw_pinref_t p;
    uint32_t us = 0u;
    tfw_status_t st = tfw_board_pin(SC_PULSE_PIN, &p);

    if (st == TFW_OK) st = tfw_hal_gpio_mode(&p, TFW_PIN_INPUT_PULLUP);
    if (st == TFW_OK) st = tfw_hal_pulse_in(&p, 0u, 50000u, &us);
    report("hal_pulse_rmt", st == TFW_ERR_TIMEOUT,
           "status %d (timeout expected), us %d", (int)st, (int)us);
}

static void check_eeprom(void)
{
    uint8_t old[16], pat[16], back[16], again[16];
    tfw_status_t st = tfw_hal_eeprom_read(SC_EEPROM_AT, old, sizeof old);
    int match = 0, restored = 0;

    for (unsigned i = 0u; i < sizeof pat; i++) {
        pat[i] = (uint8_t)(0xa5u ^ (i * 0x11u) ^ (st == TFW_OK ? old[i] : 0u));
    }
    if (st == TFW_OK) st = tfw_hal_eeprom_write(SC_EEPROM_AT, pat, sizeof pat);
    if (st == TFW_OK) st = tfw_hal_eeprom_read(SC_EEPROM_AT, back, sizeof back);
    if (st == TFW_OK) match = memcmp(pat, back, sizeof pat) == 0;
    if (st == TFW_OK) st = tfw_hal_eeprom_write(SC_EEPROM_AT, old, sizeof old);
    if (st == TFW_OK) st = tfw_hal_eeprom_read(SC_EEPROM_AT, again, sizeof again);
    if (st == TFW_OK) restored = memcmp(old, again, sizeof old) == 0;
    report("hal_eeprom", st == TFW_OK && match && restored,
           "pattern read back %d, old bytes restored %d", match, restored);
}

static void check_coproc(void)
{
#ifdef TFW_ESP_COPROC_EN_GPIO
    int level = gpio_get_level((gpio_num_t)TFW_ESP_COPROC_EN_GPIO);

    report("c6_enable", level == 0, "GPIO %d level %d",
           TFW_ESP_COPROC_EN_GPIO, level);
#else
    ESP_LOGI(TAG, "c6_enable NOT RUN the board has no coprocessor enable");
#endif
}

void tfw_esp_selfcheck(void)
{
    ESP_LOGI(TAG, "start");
    check_coproc();
    check_gpio();
    check_adc();
    check_pwm();
    check_tone();
    check_pulse();
    check_eeprom();
    ESP_LOGI(TAG, "hal_bus NOT RUN wiring needed");
    ESP_LOGI(TAG, "done");
}

#else

void tfw_esp_selfcheck(void)
{
}

#endif
