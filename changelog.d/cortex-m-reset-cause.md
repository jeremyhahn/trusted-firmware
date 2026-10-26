### Changed

- **A commanded reset reads as `SOFTWARE` on Cortex-M** (row `cortex-m`,
  #161). The reset-request marker `tfw_cm_system_reset` writes before
  `AIRCR.SYSRESETREQ` now decodes to `TFW_RESET_SOFTWARE` (5), where it read
  `TFW_RESET_UNKNOWN` (0), so SYS_INFO after `RESET` mode 0 reports that the
  firmware asked. The decode is one header, `src/hal/cortex-m/hal_reset_decode.h`,
  pinned by `test/unit/test_cm_reset_cause.c`; no status code, command id or
  wire value changes, and tfw-boot's update-request gate still opens on the
  watchdog alone. `make integration-test-cortex-m-wire`'s base row runs
  `TestQEMUResetCause` on both MPS2 boards: `POWER_ON` after QMP's reset,
  `SOFTWARE` after `RESET`, `POWER_ON` again after the next QMP reset.
