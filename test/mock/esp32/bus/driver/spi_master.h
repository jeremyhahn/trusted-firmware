/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/spi_master.h, for
 * test/unit/test_esp_bus_mutate.c alone: the types, values and calls
 * src/hal/esp32/hal_bus.c uses, with ESP-IDF's values. The test implements
 * the calls.
 */
#ifndef TFW_MOCK_ESP32_BUS_DRIVER_SPI_MASTER_H
#define TFW_MOCK_ESP32_BUS_DRIVER_SPI_MASTER_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SPI1_HOST = 0,
    SPI2_HOST = 1,
    SPI3_HOST = 2,
} spi_host_device_t;

typedef enum {
    SPI_DMA_DISABLED = 0,
    SPI_DMA_CH_AUTO  = 3,
} spi_dma_chan_t;

#define SPI_DEVICE_TXBIT_LSBFIRST (1u << 0)
#define SPI_DEVICE_RXBIT_LSBFIRST (1u << 1)
#define SPI_DEVICE_BIT_LSBFIRST   (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST)

typedef struct spi_device_t *spi_device_handle_t;

typedef struct {
    int      mosi_io_num;
    int      miso_io_num;
    int      sclk_io_num;
    int      quadwp_io_num;
    int      quadhd_io_num;
    int      max_transfer_sz;
    uint32_t flags;
} spi_bus_config_t;

typedef struct {
    uint8_t  command_bits;
    uint8_t  address_bits;
    uint8_t  dummy_bits;
    uint8_t  mode;
    uint16_t duty_cycle_pos;
    uint16_t cs_ena_pretrans;
    uint8_t  cs_ena_posttrans;
    int      clock_speed_hz;
    int      input_delay_ns;
    int      spics_io_num;
    uint32_t flags;
    int      queue_size;
} spi_device_interface_config_t;

typedef struct {
    uint32_t flags;
    uint16_t cmd;
    uint64_t addr;
    size_t   length;
    size_t   rxlength;
    void    *user;
    union {
        const void *tx_buffer;
        uint8_t     tx_data[4];
    };
    union {
        void   *rx_buffer;
        uint8_t rx_data[4];
    };
} spi_transaction_t;

esp_err_t spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_chan);
esp_err_t spi_bus_add_device(spi_host_device_t host_id,
                             const spi_device_interface_config_t *dev_config,
                             spi_device_handle_t *handle);
esp_err_t spi_bus_remove_device(spi_device_handle_t handle);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);

#endif
