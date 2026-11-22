### Added

- **Cortex-M `hal_sys` tests for every handed-off reset cause and for init**
  (#628). `test_cortexm_sys` now checks that `tfw_hal_reset_cause()` returns
  each cause tfw-boot hands off, UNKNOWN to PANIC, unchanged and without
  asking the reset handler's latch, and that `tfw_hal_init()` is `TFW_OK`.
