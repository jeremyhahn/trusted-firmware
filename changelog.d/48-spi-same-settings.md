### Fixed

- **An ESP32 SPI init at the current mode, clock and bit order keeps the
  device** (#48). It used to remove the device on every init and remake it on
  the next transfer, as the bus header says only a new configuration does;
  I2C already kept its bus at the same rate.
