### Added

- **An I2C master on the MPS2 boards** (row `cortex-m`, #161).
  `src/hal/cortex-m/mps2/hal_bus.c` bit-bangs the I2C bus over the shield 0
  SBCon (Arm DUI 0224I, 4.19). The base is `0x4020C000` on mps2-an505 (AN505)
  and `0x40029000` on mps2-an500 (AN500). The status codes are the AVR TWI
  driver's: an address or data NACK is `TFW_ERR_NACK`, and a stuck bus is
  `TFW_ERR_TIMEOUT`. The driver links with `src/caps/bus.c` only when a bus is
  on. `TFW_FEATURE_I2C` stays 0 by default, and mps2-an505 has the row
  `atecc608a:I2C=1`. SPI and 1-Wire return `TFW_ERR_UNSUPPORTED`.
  `test/unit/test_mps2_bus.c` runs the master against a simulated bus in
  `make host-test`. The row `optiga:I2C=1` measures the OPTIGA
  Trust M tier's Cortex-M cost (`docs/cortex-m/README.md`).
