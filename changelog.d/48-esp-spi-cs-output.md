### Fixed

- The ESP32 tfw_hal_spi_xfer makes its caller's chip-select an output before asserting it, as the AVR transfer does, so an SPI_XFER on a CS pin the host never set to OUTPUT selects the target instead of clocking the bytes with CS undriven; a CS the gpio driver refuses is TFW_ERR_BAD_PIN with nothing sent (Refs #48).
