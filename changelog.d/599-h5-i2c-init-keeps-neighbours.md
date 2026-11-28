### Added

- **Host tests for the STM32H5 I2C4 init keeping its neighbours** (#599).
  `test_stm32h5_i2c` now pins that `tfw_hal_i2c_init`
  (`src/hal/cortex-m/stm32h5/hal_bus.c`) keeps the other RCC_APB3ENR enables
  on (SBS, which the Ethernet port's RMII select needs), keeps the other GPIO
  bank clocks on, and leaves the mode, output type, pull and alternate
  function of GPIOB's other pins as they were. Test-only: no firmware changes.
