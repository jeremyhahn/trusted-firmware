### Added

- **`test_the_highest_seven_bit_address_reaches_the_bus` and `test_a_transfer_of_the_largest_reply_is_accepted_on_every_bus` in `test/unit/test_bus.c`** (#627): I2C_XFER to address 0x7F reaches the bus, and an I2C read, an SPI transfer, a 1-Wire write and a 1-Wire read of exactly `TFW_MAX_PAYLOAD - 1` bytes each return `TFW_OK` with the bytes the bus clocked in, so turning any of the five inclusive bounds in `tfw_i2c_cmd_xfer`, `tfw_spi_cmd_xfer`, `tfw_ow_cmd_write` or `tfw_ow_cmd_read` in `src/caps/bus.c` into an exclusive one now fails the host suite.
