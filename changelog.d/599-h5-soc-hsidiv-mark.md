### Added

- **Host tests for the STM32H5 clock raise's HSIDIV clear and the
  watchdog arm's reset mark** (#599). `test_h5_hal_soc` now pins that
  `tfw_cm_soc_early` (`src/hal/cortex-m/stm32h5/hal_soc.c`) clears both
  HSIDIV bits (an HSI / 8 start runs at HSI / 1, the other RCC_CR bits
  kept) and that `tfw_cm_soc_watchdog_arm` sets the watchdog reset mark
  before its first IWDG key. Test-only: no firmware changes.
