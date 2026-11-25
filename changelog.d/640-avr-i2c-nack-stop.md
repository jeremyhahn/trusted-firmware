### Fixed

- **AVR I2C data NACK releases the bus** (#640). `hal_bus.c` sends STOP
  when a peripheral refuses a data byte, as it already did for a refused
  address. Before, `tfw_hal_i2c_xfer` returned `TFW_ERR_NACK` with TWINT
  set and no STOP, so the TWI held SCL low until the next transfer.
