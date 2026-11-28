### Added

- **An STM32H5 gpio write is tested to be one BSRR store, not a
  read-modify-write** (#599). `test/unit/test_stm32h5_gpio.c` presets the
  bank's BSRR with another pin's word (and with every bit set in the UART's
  bank) before a write: the register holds the written pin's set or reset bit
  alone (`src/hal/cortex-m/stm32h5/hal_gpio.c`). Test only; the driver is
  unchanged.
