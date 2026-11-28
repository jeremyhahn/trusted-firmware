### Added

- **Host tests for the STM32H5 watchdog arm's IWDG_SR busy wait** (#599).
  `test_h5_hal_soc` now pins that `tfw_cm_soc_watchdog_arm`
  (`src/hal/cortex-m/stm32h5/hal_soc.c`) writes its RELOAD key only once
  both busy bits, PVU and RVU, have cleared, when they clear on different
  reads in either order. Test-only: no firmware changes.
