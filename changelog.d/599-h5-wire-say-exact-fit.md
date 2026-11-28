### Added

- **Host tests for the STM32H5 wire diagnostic's exact fit** (#599).
  `test_h5_eth_wire` now pins that a diagnostic line exactly filling the
  64-byte buffer, alone or behind a queued line, goes out whole, and that a
  line one byte longer is dropped whole
  (`src/hal/cortex-m/stm32h5/hal_eth_wire.c`).
  Test-only: no firmware changes.
