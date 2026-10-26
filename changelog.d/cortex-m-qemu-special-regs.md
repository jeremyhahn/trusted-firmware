### Added

- **A failed Cortex-M QEMU wire row's dump has the special registers** (#161).
  `qmp_regs.py` now starts QEMU's gdbstub through HMP `gdbserver` and reads
  PRIMASK, BASEPRI, FAULTMASK and CONTROL (and their Secure and Non-secure
  banks on the an505) over the GDB remote protocol. `info registers` has none
  of them. This is for the intermittent StageImage hang on the A/B row: SysTick
  is pending and not taken while the main loop runs, and the dump could not
  say what masks it. No source path in the firmware leaves interrupts masked.
  The one critical section, `tfw_hal_uart_tx_try`, saves and restores PRIMASK,
  and `tfw_cm_system_reset` masks only on its way to the reset.
