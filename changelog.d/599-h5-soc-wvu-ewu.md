### Added

- **Host tests for the STM32H5 watchdog arm's WVU and EWU busy bits** (#599).
  `test_h5_hal_soc` now pins that `tfw_cm_soc_watchdog_arm`
  (`src/hal/cortex-m/stm32h5/hal_soc.c`) writes its RELOAD key only once
  all four IWDG_SR busy bits have cleared, WVU and EWU as well as PVU and
  RVU, when WVU or EWU is the last to clear. Test-only: no firmware changes.
