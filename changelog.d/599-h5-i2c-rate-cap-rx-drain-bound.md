### Added

- **STM32H5 I2C rate cap and post-error RXDR drain bound are tested**
  (`test_stm32h5_i2c`, #599). Host tests pin that a rate above 400 kHz
  (1 MHz, `UINT32_MAX`) runs at 400 kHz, and that the post-error RXDR drain
  stops at its bound when RXNE never clears.
