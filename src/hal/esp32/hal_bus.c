/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * I2C, SPI and 1-Wire.
 *
 * I2C and SPI go through ESP-IDF's master drivers on the pins the board
 * profile names (TFW_ESP_I2C_*, TFW_ESP_SPI_*). The drivers are created on the
 * first transfer, not at init: main() inits every bus on every board, and an
 * emulator that models neither peripheral must still boot. Init records the
 * configuration; a later init with a new one tears the driver down so the
 * next transfer rebuilds it.
 *
 * 1-Wire is bit-banged on an open-drain GPIO with tfw_hal_delay_us -- the
 * one place a busy-wait is the protocol, exactly as on the AVR. The slots are
 * short enough to run with interrupts masked, which is what keeps a
 * preemption from stretching a slot past the device's window.
 *
 * SPI_CONFIG's clkdiv keeps its AVR meaning in hertz: 0..3 are 4 MHz, 1 MHz,
 * 250 kHz and 125 kHz, the rates a 16 MHz ATmega gives, so a peripheral that
 * worked on one board works on this one at the same setting.
 */

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tfw/hal.h"
#include "tfw/config.h"

#define I2C_TIMEOUT_MS 50

static i2c_master_bus_handle_t i2c_bus;
static uint32_t                i2c_hz = TFW_I2C_DEFAULT_HZ;

static spi_device_handle_t spi_dev;
static uint8_t             spi_bus_up;
static uint8_t             spi_mode;
static uint8_t             spi_clkdiv;
static uint8_t             spi_lsb;

static const uint32_t spi_hz[4] = { 4000000u, 1000000u, 250000u, 125000u };

/* ------------------------------------------------------------------ i2c */

tfw_status_t tfw_hal_i2c_init(uint32_t hz)
{
    if (i2c_bus != NULL && hz != i2c_hz) {
        (void)i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
    i2c_hz = hz;
    return TFW_OK;
}

static tfw_status_t i2c_up(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port          = -1,
        .sda_io_num        = TFW_ESP_I2C_SDA_GPIO,
        .scl_io_num        = TFW_ESP_I2C_SCL_GPIO,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    cfg.flags.enable_internal_pullup = 1;
    if (i2c_bus == NULL && i2c_new_master_bus(&cfg, &i2c_bus) != ESP_OK) {
        i2c_bus = NULL;
        return TFW_ERR_UNSUPPORTED;
    }
    return TFW_OK;
}

static tfw_status_t i2c_status(esp_err_t err)
{
    switch (err) {
    case ESP_OK:          return TFW_OK;
    case ESP_ERR_TIMEOUT: return TFW_ERR_TIMEOUT;
    default:              return TFW_ERR_NACK;
    }
}

tfw_status_t tfw_hal_i2c_xfer(uint8_t addr7, const uint8_t *wr, uint8_t wlen,
                             uint8_t *rd, uint8_t rlen)
{
    i2c_device_config_t     dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr7,
        .scl_speed_hz    = i2c_hz,
    };
    i2c_master_dev_handle_t dev;
    esp_err_t               err;
    tfw_status_t            st = i2c_up();

    if (st != TFW_OK) {
        return st;
    }
    if (wlen == 0u && rlen == 0u) {
        return i2c_status(i2c_master_probe(i2c_bus, addr7, I2C_TIMEOUT_MS));
    }
    if (i2c_master_bus_add_device(i2c_bus, &dcfg, &dev) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
    if (wlen && rlen) {
        err = i2c_master_transmit_receive(dev, wr, wlen, rd, rlen, I2C_TIMEOUT_MS);
    } else if (wlen) {
        err = i2c_master_transmit(dev, wr, wlen, I2C_TIMEOUT_MS);
    } else {
        err = i2c_master_receive(dev, rd, rlen, I2C_TIMEOUT_MS);
    }
    (void)i2c_master_bus_rm_device(dev);
    return i2c_status(err);
}

/* ------------------------------------------------------------------ spi */

tfw_status_t tfw_hal_spi_init(uint8_t mode, uint8_t clkdiv, uint8_t lsb_first)
{
    if (mode > 3u || clkdiv > 3u || lsb_first > 1u) {
        return TFW_ERR_RANGE;
    }
    if (spi_dev != NULL) {
        (void)spi_bus_remove_device(spi_dev);
        spi_dev = NULL;
    }
    spi_mode   = mode;
    spi_clkdiv = clkdiv;
    spi_lsb    = lsb_first;
    return TFW_OK;
}

static tfw_status_t spi_up(void)
{
    if (!spi_bus_up) {
        const spi_bus_config_t bus = {
            .mosi_io_num     = TFW_ESP_SPI_MOSI_GPIO,
            .miso_io_num     = TFW_ESP_SPI_MISO_GPIO,
            .sclk_io_num     = TFW_ESP_SPI_SCK_GPIO,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 64,
        };
        if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
            return TFW_ERR_UNSUPPORTED;
        }
        spi_bus_up = 1u;
    }
    if (spi_dev == NULL) {
        spi_device_interface_config_t dev = {
            .mode           = spi_mode,
            .clock_speed_hz = (int)spi_hz[spi_clkdiv],
            .spics_io_num   = -1,       /* the caller's cs pin, driven below */
            .queue_size     = 1,
        };
        if (spi_lsb) {
            dev.flags = SPI_DEVICE_BIT_LSBFIRST;
        }
        if (spi_bus_add_device(SPI2_HOST, &dev, &spi_dev) != ESP_OK) {
            spi_dev = NULL;
            return TFW_ERR_INTERNAL;
        }
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_spi_xfer(const tfw_pinref_t *cs, const uint8_t *wr,
                             uint8_t *rd, uint8_t len)
{
    spi_transaction_t t = { 0 };
    esp_err_t         err;
    tfw_status_t      st = spi_up();

    if (st != TFW_OK) {
        return st;
    }
    t.length    = (size_t)len * 8u;
    t.tx_buffer = wr;
    t.rx_buffer = rd;
    (void)gpio_set_level((gpio_num_t)cs->gpio, 0u);
    err = spi_device_polling_transmit(spi_dev, &t);
    (void)gpio_set_level((gpio_num_t)cs->gpio, 1u);
    return err == ESP_OK ? TFW_OK : TFW_ERR_INTERNAL;
}

/* --------------------------------------------------------------- 1-wire */

static void ow_low(gpio_num_t n)
{
    (void)gpio_set_direction(n, GPIO_MODE_INPUT_OUTPUT_OD);
    (void)gpio_set_level(n, 0u);
}

static void ow_release(gpio_num_t n)
{
    (void)gpio_set_level(n, 1u);
}

tfw_status_t tfw_hal_ow_reset(const tfw_pinref_t *p, uint8_t *out_presence)
{
    gpio_num_t n = (gpio_num_t)p->gpio;
    int        level;

    (void)gpio_set_pull_mode(n, GPIO_PULLUP_ONLY);
    ow_low(n);
    tfw_hal_delay_us(480u);
    taskDISABLE_INTERRUPTS();
    ow_release(n);
    tfw_hal_delay_us(70u);
    level = gpio_get_level(n);
    taskENABLE_INTERRUPTS();
    tfw_hal_delay_us(410u);
    *out_presence = (uint8_t)(level == 0);
    return TFW_OK;
}

static void ow_write_bit(gpio_num_t n, uint8_t bit)
{
    taskDISABLE_INTERRUPTS();
    ow_low(n);
    tfw_hal_delay_us(bit ? 6u : 60u);
    ow_release(n);
    taskENABLE_INTERRUPTS();
    tfw_hal_delay_us(bit ? 64u : 10u);
}

static uint8_t ow_read_bit(gpio_num_t n)
{
    int level;

    taskDISABLE_INTERRUPTS();
    ow_low(n);
    tfw_hal_delay_us(6u);
    ow_release(n);
    tfw_hal_delay_us(9u);
    level = gpio_get_level(n);
    taskENABLE_INTERRUPTS();
    tfw_hal_delay_us(55u);
    return (uint8_t)(level != 0);
}

tfw_status_t tfw_hal_ow_write(const tfw_pinref_t *p, const uint8_t *buf, uint8_t len)
{
    gpio_num_t n = (gpio_num_t)p->gpio;
    uint8_t    i;
    uint8_t    b;

    for (i = 0u; i < len; i++) {
        for (b = 0u; b < 8u; b++) {
            ow_write_bit(n, (uint8_t)((buf[i] >> b) & 1u));
        }
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_ow_read(const tfw_pinref_t *p, uint8_t *buf, uint8_t len)
{
    gpio_num_t n = (gpio_num_t)p->gpio;
    uint8_t    i;
    uint8_t    b;

    for (i = 0u; i < len; i++) {
        buf[i] = 0u;
        for (b = 0u; b < 8u; b++) {
            buf[i] = (uint8_t)(buf[i] | (uint8_t)(ow_read_bit(n) << b));
        }
    }
    return TFW_OK;
}
