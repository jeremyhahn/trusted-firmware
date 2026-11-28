### Added

- **Host tests for the STM32H5 watchdog mark read at the first IWDG key**
  (#599). `test_h5_hal_soc` now reads the reset mark at the moment
  `tfw_cm_soc_watchdog_arm` (`src/hal/cortex-m/stm32h5/hal_soc.c`) first calls
  IWDG_KR, before the ENABLE store, so a mark written after ENABLE fails the
  mark tests. Test-only: no firmware changes.
