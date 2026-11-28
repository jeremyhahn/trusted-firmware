### Added

- **Host tests for the STM32H5 pets' IWDG_SR reads** (#599).
  `test_h5_hal_soc` now pins that `tfw_cm_soc_early` and
  `tfw_cm_soc_watchdog_pet` in `src/hal/cortex-m/stm32h5/hal_soc.c` write the
  RELOAD key without reading IWDG_SR, even while an inherited watchdog's
  PVU/RVU/WVU/EWU busy bits are set, so an image entered under a running
  watchdog reloads it at once.
  Test-only: no firmware changes.
