### Changed

- **EEPROM_WRITE, I2C_CONFIG and SPI_CONFIG pass a HAL refusal back** (#627): the HAL's status (the i.MX95 LPI2C refusing a clock, the i.MX95 and MCXN94x HALs answering `UNSUPPORTED` to an SPI setting, a part that refuses a write) is the command's answer, with no reply and nothing stored or applied.
