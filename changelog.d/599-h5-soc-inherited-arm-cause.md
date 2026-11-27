### Added

- **Host tests for the STM32H5 watchdog arm over an inherited watchdog
  and for the reset cause's decode** (#599). `test_h5_hal_soc` now pins
  that `tfw_cm_soc_watchdog_arm` (`src/hal/cortex-m/stm32h5/hal_soc.c`)
  overwrites an inherited watchdog's prescaler and reload (PR /256 and
  RLR 0x7FF become PR 3 and RLR 3999) and an earlier reset mark with the
  watchdog mark, and that `tfw_cm_soc_reset_cause` returns the cause
  `tfw_h5_reset_take` decodes, not a fixed one. Test-only: no firmware
  changes.
