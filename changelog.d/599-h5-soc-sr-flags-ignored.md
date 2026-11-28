### Added

- **Host tests for the STM32H5 watchdog arm ignoring IWDG_SR status flags**
  (#599). `test_h5_hal_soc` now pins that `tfw_cm_soc_watchdog_arm`
  (`src/hal/cortex-m/stm32h5/hal_soc.c`) waits only on the four IWDG_SR busy
  bits before its RELOAD key, and does not wait on a set ONF or EWIF status
  flag, which it leaves as it found it. Test-only: no firmware changes.
