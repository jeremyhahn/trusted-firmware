### Added

- **Host tests for the STM32H5 I2C4 poll-limit waits** (#599).
  `test_stm32h5_i2c` now pins that the STM32H5 I2C4 driver
  (`src/hal/cortex-m/stm32h5/hal_bus.c`) waits out its full poll limit,
  25000 one-microsecond delays (`H5_I2C_POLL_US`, 25 ms), on a flag that never
  comes and on a STOP that never completes before it returns
  `TFW_ERR_TIMEOUT` and resets the block, rather than giving up at once.
  Test-only: no firmware changes.
