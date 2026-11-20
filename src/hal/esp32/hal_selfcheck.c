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
 * Only pins the board leaves unconnected are used, and nothing needs wiring:
 * the buses (hal_bus.c) are checked against no device -- an I2C address
 * nothing on the board answers, an SPI transfer with no peripheral selected,
 * a 1-Wire reset on a free pin -- so what runs is the HAL's own path on
 * silicon, not a peripheral's answer.
 *
 * Off -- the default, and main's image -- the function is empty.
 */

#include "hal_esp32.h"

#if CONFIG_TFW_PERIPH_SELFCHECK

#include "tfw/board.h"
#include "tfw/config.h"
#include "tfw/hal.h"

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
#define SC_SPI_CS_PIN SC_GPIO_PIN  /* no peripheral behind it */
#define SC_OW_PIN     SC_PULSE_PIN /* pulled up, nothing on it */
/* 0x08, the first address past the reserved ones: none of the devices on the
 * board's I2C (the ES8311 codec, the camera and display connectors) uses it. */
#define SC_I2C_ADDR   0x08u

/* One console line per check: name, PASS or FAIL, and two numbers named by
 * the labels, so a FAIL says what was read. */
static void report(const char *name, int pass, const char *la, int a, const char *lb, int b)
{
    ESP_LOGI(TAG, "%s %s %s %d, %s %d", name, pass ? "PASS" : "FAIL", la, a, lb, b);
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
           "high read", hi, "low read", lo);
}

static void check_adc(void)
{
    uint8_t ch = 0xffu;
    uint16_t raw = 0xffffu;
    tfw_status_t st = tfw_board_adc_channel(SC_ADC_PIN, &ch);

    if (st == TFW_OK) st = tfw_hal_adc_init();
    if (st == TFW_OK) st = tfw_hal_adc_read(ch, &raw);
    report("hal_adc", st == TFW_OK && raw <= 4095u, "status", (int)st, "raw", raw);
}

static void check_pwm(void)
{
    tfw_pwmref_t p;
    uint32_t on = 0u, off = 1u;
    tfw_status_t st = tfw_board_pwm(SC_PWM_PIN, &p);

    /* The duty register LEDC reads back latches at the start of the next PWM
     * period, so each read waits a few periods after the write. */
    if (st == TFW_OK) st = tfw_hal_pwm_write(&p, 128u);
    if (st == TFW_OK) tfw_hal_delay_us(20000u);
    if (st == TFW_OK) on = ledc_get_duty((ledc_mode_t)p.mode, (ledc_channel_t)p.channel);
    if (st == TFW_OK) st = tfw_hal_pwm_write(&p, 0u);
    if (st == TFW_OK) tfw_hal_delay_us(20000u);
    if (st == TFW_OK) off = ledc_get_duty((ledc_mode_t)p.mode, (ledc_channel_t)p.channel);
    if (st == TFW_OK) st = tfw_hal_pwm_stop(&p);
    report("hal_pwm", st == TFW_OK && on == 128u && off == 0u,
           "LEDC duty at 128:", (int)on, "at 0:", (int)off);
}

static void check_tone(void)
{
    tfw_pinref_t p;
    tfw_status_t st = tfw_board_pin(TFW_TONE_PIN, &p);
    tfw_status_t stop = TFW_ERR_INTERNAL;

    if (st == TFW_OK) st = tfw_hal_tone_start(&p, 1000u);
    if (st == TFW_OK) tfw_hal_delay_us(20000u);
    if (st == TFW_OK) stop = tfw_hal_tone_stop();
    report("hal_tone", st == TFW_OK && stop == TFW_OK, "start", (int)st, "stop", (int)stop);
}

static void check_pulse(void)
{
    tfw_pinref_t p;
    uint32_t us = 0u;
    tfw_status_t st = tfw_board_pin(SC_PULSE_PIN, &p);

    if (st == TFW_OK) st = tfw_hal_gpio_mode(&p, TFW_PIN_INPUT_PULLUP);
    if (st == TFW_OK) st = tfw_hal_pulse_in(&p, 0u, 50000u, &us);
    report("hal_pulse_rmt", st == TFW_ERR_TIMEOUT,
           "status (timeout expected)", (int)st, "us", (int)us);
}

/* A write through the HAL rewrites the whole 4 KB flash sector it touches
 * (hal_eeprom.c: read, erase, program), so a write that erases and then fails
 * to program loses the sector, not only the bytes it was given. The check
 * saves the whole sector the pattern lands in before it writes anything, and
 * once a pattern write has been tried it writes that whole sector back
 * whatever the test's status, reads it again and compares all of it; the
 * restore is reported on a line of its own. A sector that cannot be read is
 * never written. */
#define SC_SECTOR      4096u
#define SC_SECTOR_BASE (SC_EEPROM_AT - (SC_EEPROM_AT % SC_SECTOR))

static uint8_t sc_saved[SC_SECTOR];
static uint8_t sc_again[SC_SECTOR];

static void check_eeprom(void)
{
    uint32_t at = SC_EEPROM_AT - SC_SECTOR_BASE;
    uint8_t pat[16], back[16];
    tfw_status_t st = tfw_esp_part_read(TFW_ESP_PART_EEPROM, SC_SECTOR_BASE, sc_saved, SC_SECTOR);
    tfw_status_t rst;
    int match = 0, restored = 0;

    if (st != TFW_OK) {
        report("hal_eeprom", 0, "save status", (int)st, "nothing written", 0);
        return;
    }
    for (unsigned i = 0u; i < sizeof pat; i++) {
        pat[i] = (uint8_t)(0xa5u ^ (i * 0x11u) ^ sc_saved[at + i]);
    }
    st = tfw_hal_eeprom_write(SC_EEPROM_AT, pat, sizeof pat);
    if (st == TFW_OK) st = tfw_hal_eeprom_read(SC_EEPROM_AT, back, sizeof back);
    if (st == TFW_OK) match = memcmp(pat, back, sizeof pat) == 0;
    report("hal_eeprom", st == TFW_OK && match, "status", (int)st, "pattern read back", match);

    rst = tfw_esp_part_write(TFW_ESP_PART_EEPROM, SC_SECTOR_BASE, sc_saved, SC_SECTOR);
    if (rst == TFW_OK) rst = tfw_esp_part_read(TFW_ESP_PART_EEPROM, SC_SECTOR_BASE, sc_again, SC_SECTOR);
    if (rst == TFW_OK) restored = memcmp(sc_saved, sc_again, SC_SECTOR) == 0;
    report("hal_eeprom_restore", rst == TFW_OK && restored,
           "status", (int)rst, "sector restored", restored);
}

/* An address probe nobody answers: the driver comes up on the board's pins and
 * the bus returns a NACK. A timeout is a bus held low; an ACK is a device at an
 * address the check takes to be empty. Both fail. */
static void check_i2c(void)
{
    tfw_status_t st = tfw_hal_i2c_init(TFW_I2C_DEFAULT_HZ);

    if (st == TFW_OK) st = tfw_hal_i2c_xfer(SC_I2C_ADDR, NULL, 0u, NULL, 0u);
    report("hal_bus_i2c", st == TFW_ERR_NACK,
           "probe status (NACK expected)", (int)st, "address", SC_I2C_ADDR);
}

/* A two-byte transfer with the chip select on a free pin: the bus comes up and
 * the transfer completes. MISO is floating, so what it reads is not checked. */
static void check_spi(void)
{
    static const uint8_t wr[2] = { 0xa5u, 0x5au };
    uint8_t rd[2];
    tfw_pinref_t cs;
    tfw_status_t pin = tfw_board_pin(SC_SPI_CS_PIN, &cs);
    tfw_status_t st = pin;

    if (st == TFW_OK) st = tfw_hal_gpio_mode(&cs, TFW_PIN_OUTPUT);
    if (st == TFW_OK) st = tfw_hal_gpio_write(&cs, 1u);
    if (st == TFW_OK) st = tfw_hal_spi_init(0u, 1u, 0u);
    if (st == TFW_OK) st = tfw_hal_spi_xfer(&cs, wr, rd, sizeof wr);
    if (pin == TFW_OK) (void)tfw_hal_gpio_mode(&cs, TFW_PIN_INPUT);
    report("hal_bus_spi", st == TFW_OK, "status", (int)st, "bytes", (int)sizeof wr);
}

/* A reset on a pulled-up pin with nothing on it: no presence pulse, and a
 * byte read is all ones, the idle line. */
static void check_ow(void)
{
    tfw_pinref_t p;
    uint8_t presence = 0xffu, b = 0u;
    tfw_status_t st = tfw_board_pin(SC_OW_PIN, &p);

    if (st == TFW_OK) st = tfw_hal_ow_reset(&p, &presence);
    if (st == TFW_OK) st = tfw_hal_ow_read(&p, &b, 1u);
    report("hal_bus_1wire", st == TFW_OK && presence == 0u && b == 0xffu,
           "presence (0 expected)", presence, "byte read", b);
}

/* tfw_esp_board_init() drives the enable low as an output and holds it, which
 * leaves the pad's input buffer off, and gpio_get_level() then reads 0
 * whatever the pin's level. The check releases the hold, turns the pad to
 * input-and-output with the output still driven low, reads the pad, and holds
 * it again, so the level read is the pad's own. */
static void check_coproc(void)
{
#ifdef TFW_ESP_COPROC_EN_GPIO
    gpio_num_t en = (gpio_num_t)TFW_ESP_COPROC_EN_GPIO;
    int ok = gpio_hold_dis(en) == ESP_OK &&
             gpio_set_level(en, 0u) == ESP_OK &&
             gpio_set_direction(en, GPIO_MODE_INPUT_OUTPUT) == ESP_OK;
    int level = ok ? gpio_get_level(en) : -1;

    ok = gpio_hold_en(en) == ESP_OK && ok;
    report("c6_enable", ok && level == 0, "GPIO", TFW_ESP_COPROC_EN_GPIO, "pad level", level);
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
    check_i2c();
    check_spi();
    check_ow();
    ESP_LOGI(TAG, "done");
}

#else

void tfw_esp_selfcheck(void)
{
}

#endif
