/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 I2C, SPI and 1-Wire, src/hal/esp32/hal_bus.c.
 *
 * The unit had no host build; test_bus_mutate builds the core layer's bus
 * commands over the mock HAL, not this. This builds it over stand-ins for
 * ESP-IDF's gpio, I2C master and SPI master drivers and FreeRTOS's
 * interrupt mask (test/mock/esp32/bus/), every call logged here in one trace
 * and able to fail, and holds:
 *
 *   - I2C: the bus made on the first transfer on the profile's pins with the
 *     internal pull-ups, made once, a failed one unsupported and tried again;
 *     an empty transfer a probe; a write, a read or both as one device added
 *     at the current rate, the one matching call, and the device removed;
 *     ESP_OK ok, a timeout a timeout, anything else a NACK; a new rate
 *     tearing the bus down, the same rate or no bus leaving it;
 *   - SPI: a mode, clock or bit order out of range refused and the settings
 *     kept; the bus made once on SPI2 without DMA on the profile's pins; the
 *     device made at the mode, the AVR rate for the clock and the bit order,
 *     again only after an init; a failed bus unsupported, a failed device
 *     internal; a transfer as cs low, the bytes, cs high, even when it fails;
 *   - 1-Wire: each reset, bit written and bit read as its exact sequence of
 *     drive, release, delay, sample and interrupt mask; bytes LSB first.
 *
 * The unit's driver handles are static, so the tests run in the order
 * main() lists.
 */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "tfw/hal.h"
#include "tfw/config.h"

enum {
    E_DIR = 1, E_PULL, E_SET, E_GET, E_DELAY, E_IRQ_OFF, E_IRQ_ON,
    E_BUS_NEW, E_BUS_DEL, E_PROBE, E_DEV_ADD, E_DEV_RM, E_TXRX, E_TX, E_RX,
    E_SPI_BUS, E_SPI_ADD, E_SPI_RM, E_SPI_XFER,
};

typedef struct {
    int  op;
    long a;
    long b;
} ev_t;

#define TRACE_MAX 1024u

static ev_t     trace[TRACE_MAX];
static unsigned ntrace;

static void log_ev(int op, long a, long b)
{
    TEST_ASSERT_LESS_THAN_UINT(TRACE_MAX, ntrace);
    trace[ntrace].op = op;
    trace[ntrace].a  = a;
    trace[ntrace].b  = b;
    ntrace++;
}

static unsigned count_op(int op)
{
    unsigned i, n = 0u;

    for (i = 0u; i < ntrace; i++) {
        n += trace[i].op == op;
    }
    return n;
}

static const ev_t *find_op(int op)
{
    unsigned i;

    for (i = 0u; i < ntrace; i++) {
        if (trace[i].op == op) {
            return &trace[i];
        }
    }
    TEST_FAIL_MESSAGE("call not made");
    return NULL;
}

/* ---------------------------------------------------------------- gpio + rtos */

static int      levels_in[256];
static unsigned nlevels, level_at;

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    log_ev(E_DIR, gpio_num, (long)mode);
    return ESP_OK;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    log_ev(E_PULL, gpio_num, (long)pull);
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    log_ev(E_SET, gpio_num, (long)level);
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    log_ev(E_GET, gpio_num, 0);
    TEST_ASSERT_LESS_THAN_UINT(nlevels, level_at);
    return levels_in[level_at++];
}

void tfw_hal_delay_us(uint16_t us)
{
    log_ev(E_DELAY, us, 0);
}

void mock_irq_disable(void) { log_ev(E_IRQ_OFF, 0, 0); }
void mock_irq_enable(void)  { log_ev(E_IRQ_ON, 0, 0); }

/* ---------------------------------------------------------------------- i2c */

#define BUS(n) ((i2c_master_bus_handle_t)(uintptr_t)(0x1000u + (n)))
#define DEV    ((i2c_master_dev_handle_t)(uintptr_t)0x2000u)

static unsigned                nbus_made;
static i2c_master_bus_config_t bus_cfg;
static i2c_device_config_t     dev_cfg;
static esp_err_t               bus_err, add_err, xfer_err;
static const uint8_t          *wr_seen;
static uint8_t                *rd_seen;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                             i2c_master_bus_handle_t *ret_bus_handle)
{
    bus_cfg = *bus_config;
    log_ev(E_BUS_NEW, 0, 0);
    /* A failed call writes a handle anyway: the unit must not keep it. */
    *ret_bus_handle = BUS(++nbus_made);
    return bus_err;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
    log_ev(E_BUS_DEL, (long)(uintptr_t)bus_handle, 0);
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address,
                           int xfer_timeout_ms)
{
    TEST_ASSERT_EQUAL_INT(50, xfer_timeout_ms);
    log_ev(E_PROBE, (long)(uintptr_t)bus_handle, address);
    return xfer_err;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle)
{
    dev_cfg = *dev_config;
    log_ev(E_DEV_ADD, (long)(uintptr_t)bus_handle, 0);
    *ret_handle = DEV;
    return add_err;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    log_ev(E_DEV_RM, (long)(uintptr_t)handle, 0);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                      const uint8_t *write_buffer, size_t write_size,
                                      uint8_t *read_buffer, size_t read_size,
                                      int xfer_timeout_ms)
{
    TEST_ASSERT_EQUAL_PTR(DEV, i2c_dev);
    TEST_ASSERT_EQUAL_INT(50, xfer_timeout_ms);
    wr_seen = write_buffer;
    rd_seen = read_buffer;
    log_ev(E_TXRX, (long)write_size, (long)read_size);
    return xfer_err;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms)
{
    TEST_ASSERT_EQUAL_PTR(DEV, i2c_dev);
    TEST_ASSERT_EQUAL_INT(50, xfer_timeout_ms);
    wr_seen = write_buffer;
    log_ev(E_TX, (long)write_size, 0);
    return xfer_err;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev, uint8_t *read_buffer,
                             size_t read_size, int xfer_timeout_ms)
{
    TEST_ASSERT_EQUAL_PTR(DEV, i2c_dev);
    TEST_ASSERT_EQUAL_INT(50, xfer_timeout_ms);
    rd_seen = read_buffer;
    log_ev(E_RX, (long)read_size, 0);
    return xfer_err;
}

/* ---------------------------------------------------------------------- spi */

#define SPIDEV(n) ((spi_device_handle_t)(uintptr_t)(0x3000u + (n)))

static unsigned                      nspi_made;
static spi_bus_config_t              spi_bus_cfg;
static spi_device_interface_config_t spi_dev_cfg;
static spi_transaction_t             spi_t;
static esp_err_t                     spi_bus_err, spi_add_err, spi_xfer_err;

esp_err_t spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_chan)
{
    spi_bus_cfg = *bus_config;
    log_ev(E_SPI_BUS, (long)host_id, (long)dma_chan);
    return spi_bus_err;
}

esp_err_t spi_bus_add_device(spi_host_device_t host_id,
                             const spi_device_interface_config_t *dev_config,
                             spi_device_handle_t *handle)
{
    spi_dev_cfg = *dev_config;
    log_ev(E_SPI_ADD, (long)host_id, 0);
    *handle = SPIDEV(++nspi_made);
    return spi_add_err;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t handle)
{
    log_ev(E_SPI_RM, (long)(uintptr_t)handle, 0);
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc)
{
    spi_t = *trans_desc;
    log_ev(E_SPI_XFER, (long)(uintptr_t)handle, 0);
    return spi_xfer_err;
}

/* -------------------------------------------------------------------- tests */

void setUp(void)
{
    ntrace  = 0u;
    nlevels = level_at = 0u;
    bus_err = add_err = xfer_err = ESP_OK;
    spi_bus_err = spi_add_err = spi_xfer_err = ESP_OK;
    memset(&bus_cfg, 0, sizeof bus_cfg);
    memset(&dev_cfg, 0, sizeof dev_cfg);
    memset(&spi_bus_cfg, 0, sizeof spi_bus_cfg);
    memset(&spi_dev_cfg, 0x5A, sizeof spi_dev_cfg);
    memset(&spi_t, 0x5A, sizeof spi_t);
    wr_seen = NULL;
    rd_seen = NULL;
}

void tearDown(void) {}

static const uint8_t wbuf[3] = { 1u, 2u, 3u };
static uint8_t       rbuf[4];

static void test_a_failed_i2c_bus_is_unsupported_and_tried_again(void)
{
    unsigned i;

    bus_err = ESP_FAIL;
    for (i = 0u; i < 2u; i++) {
        ntrace = 0u;
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_i2c_xfer(0x50u, wbuf, 1u, NULL, 0u));
        TEST_ASSERT_EQUAL_UINT(1u, ntrace);
        TEST_ASSERT_EQUAL_INT(E_BUS_NEW, trace[0].op);
    }
    ntrace = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_i2c_xfer(0x50u, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, ntrace);
}

static void test_the_i2c_bus_is_made_once_on_the_profiles_pins(void)
{
    nbus_made = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x50u, wbuf, 2u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, count_op(E_BUS_NEW));
    TEST_ASSERT_EQUAL_INT(-1, bus_cfg.i2c_port);
    TEST_ASSERT_EQUAL_INT(TFW_ESP_I2C_SDA_GPIO, bus_cfg.sda_io_num);
    TEST_ASSERT_EQUAL_INT(TFW_ESP_I2C_SCL_GPIO, bus_cfg.scl_io_num);
    TEST_ASSERT_EQUAL(I2C_CLK_SRC_DEFAULT, bus_cfg.clk_source);
    TEST_ASSERT_EQUAL_UINT8(7u, bus_cfg.glitch_ignore_cnt);
    TEST_ASSERT_EQUAL_UINT(1u, bus_cfg.flags.enable_internal_pullup);
    /* No init yet: the default rate. */
    TEST_ASSERT_EQUAL_UINT32(TFW_I2C_DEFAULT_HZ, dev_cfg.scl_speed_hz);
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(1), find_op(E_DEV_ADD)->a);

    ntrace = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x51u, NULL, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x51u, wbuf, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, count_op(E_BUS_NEW));
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(1), find_op(E_PROBE)->a);
}

static void test_an_empty_i2c_transfer_is_a_probe(void)
{
    static const struct {
        esp_err_t    err;
        tfw_status_t want;
    } t[] = {
        { ESP_OK, TFW_OK }, { ESP_ERR_TIMEOUT, TFW_ERR_TIMEOUT },
        { ESP_ERR_NOT_FOUND, TFW_ERR_NACK }, { ESP_FAIL, TFW_ERR_NACK },
        { ESP_ERR_INVALID_ARG, TFW_ERR_NACK },
    };
    unsigned i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        ntrace   = 0u;
        xfer_err = t[i].err;
        TEST_ASSERT_EQUAL(t[i].want, tfw_hal_i2c_xfer(0x3Cu, NULL, 0u, NULL, 0u));
        TEST_ASSERT_EQUAL_UINT(1u, ntrace);
        TEST_ASSERT_EQUAL_INT(E_PROBE, trace[0].op);
        TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(1), trace[0].a);
        TEST_ASSERT_EQUAL_INT(0x3C, trace[0].b);
    }
}

static void assert_device_cycle(int op, long a, long b, uint8_t addr)
{
    TEST_ASSERT_EQUAL_UINT(3u, ntrace);
    TEST_ASSERT_EQUAL_INT(E_DEV_ADD, trace[0].op);
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(1), trace[0].a);
    TEST_ASSERT_EQUAL(I2C_ADDR_BIT_LEN_7, dev_cfg.dev_addr_length);
    TEST_ASSERT_EQUAL_UINT16(addr, dev_cfg.device_address);
    TEST_ASSERT_EQUAL_INT(op, trace[1].op);
    TEST_ASSERT_EQUAL_INT(a, trace[1].a);
    TEST_ASSERT_EQUAL_INT(b, trace[1].b);
    TEST_ASSERT_EQUAL_INT(E_DEV_RM, trace[2].op);
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)DEV, trace[2].a);
}

static void test_an_i2c_write_read_or_both_is_one_device_cycle(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x68u, wbuf, 3u, NULL, 0u));
    assert_device_cycle(E_TX, 3, 0, 0x68u);
    TEST_ASSERT_EQUAL_PTR(wbuf, wr_seen);

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x29u, NULL, 0u, rbuf, 4u));
    assert_device_cycle(E_RX, 4, 0, 0x29u);
    TEST_ASSERT_EQUAL_PTR(rbuf, rd_seen);

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x7Fu, wbuf, 1u, rbuf, 2u));
    assert_device_cycle(E_TXRX, 1, 2, 0x7Fu);
    TEST_ASSERT_EQUAL_PTR(wbuf, wr_seen);
    TEST_ASSERT_EQUAL_PTR(rbuf, rd_seen);

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x01u, wbuf, 255u, rbuf, 255u));
    assert_device_cycle(E_TXRX, 255, 255, 0x01u);
}

static void test_an_i2c_error_is_mapped_and_the_device_removed(void)
{
    static const esp_err_t errs[] = { ESP_ERR_TIMEOUT, ESP_FAIL, ESP_ERR_INVALID_ARG };
    static const tfw_status_t want[] = { TFW_ERR_TIMEOUT, TFW_ERR_NACK, TFW_ERR_NACK };
    unsigned i;

    for (i = 0u; i < 3u; i++) {
        setUp();
        xfer_err = errs[i];
        TEST_ASSERT_EQUAL(want[i], tfw_hal_i2c_xfer(0x10u, wbuf, 2u, NULL, 0u));
        assert_device_cycle(E_TX, 2, 0, 0x10u);
        setUp();
        xfer_err = errs[i];
        TEST_ASSERT_EQUAL(want[i], tfw_hal_i2c_xfer(0x10u, NULL, 0u, rbuf, 1u));
        assert_device_cycle(E_RX, 1, 0, 0x10u);
        setUp();
        xfer_err = errs[i];
        TEST_ASSERT_EQUAL(want[i], tfw_hal_i2c_xfer(0x10u, wbuf, 1u, rbuf, 1u));
        assert_device_cycle(E_TXRX, 1, 1, 0x10u);
    }
}

static void test_a_device_the_bus_refuses_is_internal(void)
{
    add_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_i2c_xfer(0x10u, wbuf, 2u, rbuf, 2u));
    TEST_ASSERT_EQUAL_UINT(1u, ntrace);
    TEST_ASSERT_EQUAL_INT(E_DEV_ADD, trace[0].op);
}

static void test_a_new_i2c_rate_rebuilds_the_bus(void)
{
    /* the same rate as the bus was made at: kept */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(TFW_I2C_DEFAULT_HZ));
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(400000u));
    TEST_ASSERT_EQUAL_UINT(1u, ntrace);
    TEST_ASSERT_EQUAL_INT(E_BUS_DEL, trace[0].op);
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(1), trace[0].a);

    /* no bus to tear down: a second new rate only records it */
    ntrace = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(1000000u));
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x20u, wbuf, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT(1u, count_op(E_BUS_NEW));
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(2), find_op(E_DEV_ADD)->a);
    TEST_ASSERT_EQUAL_UINT32(1000000u, dev_cfg.scl_speed_hz);

    /* a rate set while the bus is up is the next device's rate */
    ntrace = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(1000000u));
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_init(100001u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_i2c_xfer(0x20u, wbuf, 1u, NULL, 0u));
    TEST_ASSERT_EQUAL_UINT32(100001u, dev_cfg.scl_speed_hz);
    TEST_ASSERT_EQUAL_INT((long)(uintptr_t)BUS(3), find_op(E_DEV_ADD)->a);
}

static const tfw_pinref_t cs = { 17u };

static void assert_spi_xfer(size_t bits, const void *tx, void *rx, long dev)
{
    const ev_t *x = find_op(E_SPI_XFER);
    unsigned    i = (unsigned)(x - trace);

    TEST_ASSERT_GREATER_THAN_UINT(0u, i);
    TEST_ASSERT_LESS_THAN_UINT(ntrace - 1u, i);
    TEST_ASSERT_EQUAL_INT(E_SET, trace[i - 1u].op);
    TEST_ASSERT_EQUAL_INT(17, trace[i - 1u].a);
    TEST_ASSERT_EQUAL_INT(0, trace[i - 1u].b);
    TEST_ASSERT_EQUAL_INT(E_SET, trace[i + 1u].op);
    TEST_ASSERT_EQUAL_INT(17, trace[i + 1u].a);
    TEST_ASSERT_EQUAL_INT(1, trace[i + 1u].b);
    TEST_ASSERT_EQUAL_UINT(i + 2u, ntrace);
    TEST_ASSERT_EQUAL_INT(dev, x->a);
    TEST_ASSERT_EQUAL_UINT((unsigned)bits, (unsigned)spi_t.length);
    TEST_ASSERT_EQUAL_PTR(tx, spi_t.tx_buffer);
    TEST_ASSERT_EQUAL_PTR(rx, spi_t.rx_buffer);
    TEST_ASSERT_EQUAL_UINT32(0u, spi_t.flags);
    TEST_ASSERT_EQUAL_UINT((unsigned)0u, (unsigned)spi_t.rxlength);
}

static void test_a_failed_spi_bus_is_unsupported_and_tried_again(void)
{
    unsigned i;

    spi_bus_err = ESP_FAIL;
    for (i = 0u; i < 2u; i++) {
        ntrace = 0u;
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 3u));
        TEST_ASSERT_EQUAL_UINT(1u, ntrace);
        TEST_ASSERT_EQUAL_INT(E_SPI_BUS, trace[0].op);
    }
}

static void test_a_failed_spi_device_is_internal_and_tried_again(void)
{
    spi_add_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 3u));
    TEST_ASSERT_EQUAL_UINT(2u, ntrace);
    TEST_ASSERT_EQUAL_INT(E_SPI_BUS, trace[0].op);
    TEST_ASSERT_EQUAL_INT(SPI2_HOST, trace[0].a);
    TEST_ASSERT_EQUAL_INT(SPI_DMA_DISABLED, trace[0].b);
    TEST_ASSERT_EQUAL_INT(TFW_ESP_SPI_MOSI_GPIO, spi_bus_cfg.mosi_io_num);
    TEST_ASSERT_EQUAL_INT(TFW_ESP_SPI_MISO_GPIO, spi_bus_cfg.miso_io_num);
    TEST_ASSERT_EQUAL_INT(TFW_ESP_SPI_SCK_GPIO, spi_bus_cfg.sclk_io_num);
    TEST_ASSERT_EQUAL_INT(-1, spi_bus_cfg.quadwp_io_num);
    TEST_ASSERT_EQUAL_INT(-1, spi_bus_cfg.quadhd_io_num);
    TEST_ASSERT_EQUAL_INT(64, spi_bus_cfg.max_transfer_sz);
    TEST_ASSERT_EQUAL_INT(E_SPI_ADD, trace[1].op);

    /* the bus stays up; a failed device left no handle behind */
    setUp();
    spi_add_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 3u));
    TEST_ASSERT_EQUAL_UINT(1u, ntrace);
    TEST_ASSERT_EQUAL_INT(E_SPI_ADD, trace[0].op);
}

static void test_the_spi_device_is_made_once_at_the_defaults(void)
{
    nspi_made = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 3u));
    TEST_ASSERT_EQUAL_INT(E_SPI_ADD, trace[0].op);
    TEST_ASSERT_EQUAL_INT(SPI2_HOST, trace[0].a);
    TEST_ASSERT_EQUAL_UINT8(0u, spi_dev_cfg.mode);
    TEST_ASSERT_EQUAL_INT(4000000, spi_dev_cfg.clock_speed_hz);
    TEST_ASSERT_EQUAL_INT(-1, spi_dev_cfg.spics_io_num);
    TEST_ASSERT_EQUAL_INT(1, spi_dev_cfg.queue_size);
    TEST_ASSERT_EQUAL_HEX32(0u, spi_dev_cfg.flags);
    TEST_ASSERT_EQUAL_UINT8(0u, spi_dev_cfg.command_bits);
    TEST_ASSERT_EQUAL_UINT8(0u, spi_dev_cfg.address_bits);
    TEST_ASSERT_EQUAL_UINT8(0u, spi_dev_cfg.dummy_bits);
    ntrace--;   /* the add, ahead of the transfer */
    memmove(trace, trace + 1, ntrace * sizeof trace[0]);
    assert_spi_xfer(24u, wbuf, rbuf, (long)(uintptr_t)SPIDEV(1));

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, NULL, rbuf, 1u));
    assert_spi_xfer(8u, NULL, rbuf, (long)(uintptr_t)SPIDEV(1));
    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wbuf, NULL, 255u));
    assert_spi_xfer(2040u, wbuf, NULL, (long)(uintptr_t)SPIDEV(1));
    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 0u));
    assert_spi_xfer(0u, wbuf, rbuf, (long)(uintptr_t)SPIDEV(1));
}

static void test_a_failed_spi_transfer_is_internal_with_cs_released(void)
{
    static const esp_err_t errs[] = { ESP_FAIL, ESP_ERR_TIMEOUT };
    unsigned i;

    for (i = 0u; i < 2u; i++) {
        setUp();
        spi_xfer_err = errs[i];
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 2u));
        assert_spi_xfer(16u, wbuf, rbuf, (long)(uintptr_t)SPIDEV(1));
    }
}

static void test_spi_settings_out_of_range_are_refused_and_kept(void)
{
    static const uint8_t bad[][3] = {
        { 4u, 0u, 0u }, { 0u, 4u, 0u }, { 0u, 0u, 2u }, { 255u, 255u, 255u },
    };
    unsigned i;

    for (i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_spi_init(bad[i][0], bad[i][1], bad[i][2]));
    }
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);   /* the device is kept */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, count_op(E_SPI_ADD));
}

static void test_an_spi_init_remakes_the_device_at_its_settings(void)
{
    static const struct {
        uint8_t  mode, clkdiv, lsb;
        int      hz;
        uint32_t flags;
    } t[] = {
        { 3u, 3u, 1u, 125000, SPI_DEVICE_BIT_LSBFIRST },
        { 1u, 1u, 0u, 1000000, 0u },
        { 2u, 2u, 1u, 250000, SPI_DEVICE_BIT_LSBFIRST },
        { 0u, 0u, 0u, 4000000, 0u },
    };
    unsigned i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        setUp();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_init(t[i].mode, t[i].clkdiv, t[i].lsb));
        TEST_ASSERT_EQUAL_UINT(1u, ntrace);
        TEST_ASSERT_EQUAL_INT(E_SPI_RM, trace[0].op);
        TEST_ASSERT_EQUAL_INT((long)(uintptr_t)SPIDEV(i + 1u), trace[0].a);

        /* no device now: a second init removes nothing */
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_init(t[i].mode, t[i].clkdiv, t[i].lsb));
        TEST_ASSERT_EQUAL_UINT(1u, ntrace);

        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_spi_xfer(&cs, wbuf, rbuf, 1u));
        TEST_ASSERT_EQUAL_UINT(0u, count_op(E_SPI_BUS));
        TEST_ASSERT_EQUAL_UINT(1u, count_op(E_SPI_ADD));
        TEST_ASSERT_EQUAL_UINT8(t[i].mode, spi_dev_cfg.mode);
        TEST_ASSERT_EQUAL_INT(t[i].hz, spi_dev_cfg.clock_speed_hz);
        TEST_ASSERT_EQUAL_HEX32(t[i].flags, spi_dev_cfg.flags);
        TEST_ASSERT_EQUAL_INT((long)(uintptr_t)SPIDEV(i + 2u), find_op(E_SPI_XFER)->a);
    }
}

/* ------------------------------------------------------------------- 1-wire */

static ev_t     want_tr[TRACE_MAX];
static unsigned nwant;

static void want(int op, long a, long b)
{
    TEST_ASSERT_LESS_THAN_UINT(TRACE_MAX, nwant);
    want_tr[nwant].op = op;
    want_tr[nwant].a  = a;
    want_tr[nwant].b  = b;
    nwant++;
}

static void want_low(long n)
{
    want(E_DIR, n, GPIO_MODE_INPUT_OUTPUT_OD);
    want(E_SET, n, 0);
}

static void want_write_bit(long n, unsigned bit)
{
    want(E_IRQ_OFF, 0, 0);
    want_low(n);
    want(E_DELAY, bit ? 6 : 60, 0);
    want(E_SET, n, 1);
    want(E_IRQ_ON, 0, 0);
    want(E_DELAY, bit ? 64 : 10, 0);
}

static void want_read_bit(long n)
{
    want(E_IRQ_OFF, 0, 0);
    want_low(n);
    want(E_DELAY, 6, 0);
    want(E_SET, n, 1);
    want(E_DELAY, 9, 0);
    want(E_GET, n, 0);
    want(E_IRQ_ON, 0, 0);
    want(E_DELAY, 55, 0);
}

static void assert_trace_is_wanted(void)
{
    unsigned i;

    TEST_ASSERT_EQUAL_UINT(nwant, ntrace);
    for (i = 0u; i < nwant; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_tr[i].op, trace[i].op, "op");
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_tr[i].a, trace[i].a, "a");
        TEST_ASSERT_EQUAL_INT_MESSAGE(want_tr[i].b, trace[i].b, "b");
    }
}

static void test_a_reset_is_its_slot_and_the_presence(void)
{
    static const struct {
        int     level;
        uint8_t presence;
    } t[] = { { 0, 1u }, { 1, 0u }, { 2, 0u }, { -1, 0u } };
    const tfw_pinref_t p = { 4u };
    unsigned i;

    for (i = 0u; i < 4u; i++) {
        uint8_t presence = 0xAAu;

        setUp();
        nwant        = 0u;
        levels_in[0] = t[i].level;
        nlevels      = 1u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_reset(&p, &presence));
        TEST_ASSERT_EQUAL_UINT8(t[i].presence, presence);
        want(E_PULL, 4, GPIO_PULLUP_ONLY);
        want_low(4);
        want(E_DELAY, 480, 0);
        want(E_IRQ_OFF, 0, 0);
        want(E_SET, 4, 1);
        want(E_DELAY, 70, 0);
        want(E_GET, 4, 0);
        want(E_IRQ_ON, 0, 0);
        want(E_DELAY, 410, 0);
        assert_trace_is_wanted();
    }
}

static void test_a_write_is_each_bit_lsb_first(void)
{
    static const uint8_t buf[] = { 0xA5u, 0x01u, 0x80u };
    const tfw_pinref_t   p = { 33u };
    unsigned i, b;

    nwant = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_write(&p, buf, 3u));
    for (i = 0u; i < 3u; i++) {
        for (b = 0u; b < 8u; b++) {
            want_write_bit(33, (buf[i] >> b) & 1u);
        }
    }
    assert_trace_is_wanted();

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_write(&p, buf, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);
}

static void test_a_read_is_each_bit_lsb_first(void)
{
    static const uint8_t want_buf[] = { 0x5Bu, 0x13u };
    const tfw_pinref_t   p = { 26u };
    uint8_t  buf[3] = { 0xFFu, 0xFFu, 0xEEu };
    unsigned i, b;

    nwant   = 0u;
    nlevels = 16u;
    for (i = 0u; i < 2u; i++) {
        for (b = 0u; b < 8u; b++) {
            /* a high bit read as 1 or as any non-zero level */
            levels_in[i * 8u + b] = ((want_buf[i] >> b) & 1u) ? (int)(1u + (b & 1u)) : 0;
            want_read_bit(26);
        }
    }
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_read(&p, buf, 2u));
    TEST_ASSERT_EQUAL_HEX8(0x5Bu, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x13u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[2]);
    assert_trace_is_wanted();

    setUp();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_ow_read(&p, buf, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, ntrace);
    TEST_ASSERT_EQUAL_HEX8(0x5Bu, buf[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_i2c_bus_is_unsupported_and_tried_again);
    RUN_TEST(test_the_i2c_bus_is_made_once_on_the_profiles_pins);
    RUN_TEST(test_an_empty_i2c_transfer_is_a_probe);
    RUN_TEST(test_an_i2c_write_read_or_both_is_one_device_cycle);
    RUN_TEST(test_an_i2c_error_is_mapped_and_the_device_removed);
    RUN_TEST(test_a_device_the_bus_refuses_is_internal);
    RUN_TEST(test_a_new_i2c_rate_rebuilds_the_bus);
    RUN_TEST(test_a_failed_spi_bus_is_unsupported_and_tried_again);
    RUN_TEST(test_a_failed_spi_device_is_internal_and_tried_again);
    RUN_TEST(test_the_spi_device_is_made_once_at_the_defaults);
    RUN_TEST(test_a_failed_spi_transfer_is_internal_with_cs_released);
    RUN_TEST(test_spi_settings_out_of_range_are_refused_and_kept);
    RUN_TEST(test_an_spi_init_remakes_the_device_at_its_settings);
    RUN_TEST(test_a_reset_is_its_slot_and_the_presence);
    RUN_TEST(test_a_write_is_each_bit_lsb_first);
    RUN_TEST(test_a_read_is_each_bit_lsb_first);
    return UNITY_END();
}
