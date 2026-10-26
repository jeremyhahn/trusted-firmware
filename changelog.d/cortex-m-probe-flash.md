### Added

- **Cortex-M silicon boards are written through their debug probes, record
  first** (SPEC row `cortex-m`). Each board's `profile.mk` names one tool:
  STM32CubeProgrammer for `stm32h573i-dk`, `picotool` for `rp2350-pico2w`,
  J-Link Commander for `nrf54l15-dk` and pyOCD for `frdm-mcxn947`.
  `make board-record CM_PROBE_SERIAL=<serial>` reads the whole flash, and the
  option bytes, OTP or UICR where the tool can, into `build/records/`, and
  checks the length. `make flash` and `make boot-flash` refuse to write without
  that record and compare every write on read-back. `make board-restore` writes
  a named record back. No option-byte, protection, lock, OTP or fuse operation
  is run, and probe arguments naming one are refused. A missing tool fails with
  its name and how to install it. `make clean` keeps `build/records/`. Not yet
  run on any board.
