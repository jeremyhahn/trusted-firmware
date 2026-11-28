### Added

- **Host tests for the STM32H5 reset cause behind the request mark** (#599,
  `src/hal/cortex-m/stm32h5/hal_soc.c`). `test_stm32h5_soc` now pins that a
  software reset is reported as software only when the firmware's
  reset-request mark is in, that a watchdog reset is the watchdog whatever
  the mark, and that each read clears RCC_RSR.
  Test-only: no firmware changes.
