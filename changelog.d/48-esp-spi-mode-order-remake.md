### Added

- **A host test holds that an ESP32 SPI init changing only the mode, or only
  the bit order, remakes the device** (#48). Every earlier test changed the
  clock too, so dropping either term of spi_init's settings check left them
  green while the next transfer ran at the old clock polarity or bit order.
