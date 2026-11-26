### Added

- **A refused STM32H5 I2C rate is tested on a running bus**
  (`test_stm32h5_i2c`, #599). After a 400 kHz init, `tfw_hal_i2c_init(99999)`
  returns RANGE without touching a register: PE stays set, TIMINGR keeps its
  400 kHz value, GPIOB is unchanged, and the next write still goes out.
