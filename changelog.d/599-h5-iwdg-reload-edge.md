### Added

- **Host tests for the STM32H5 IWDG timing's 12-bit reload edge** (#599).
  `test_stm32h5_calc` now pins a reload of 0xFFF at the first and last
  divider, the next divider taken one tick past it, a period one tick past
  the last divider refused untouched, and a zero LSI clock refused untouched.
  Test-only: no firmware changes.
