### Added

- **Host tests for which STM32H5 SoC hook writes the reset mark** (#599).
  `test_h5_hal_soc` now pins that only `tfw_cm_soc_watchdog_arm` writes the
  reset mark in `src/hal/cortex-m/stm32h5/hal_soc.c`: `tfw_cm_soc_early`
  leaves the mark hal_early cleared, and `tfw_cm_soc_watchdog_pet` keeps an
  armed watchdog's mark, so a later reset is read for what it was.
  Test-only: no firmware changes.
