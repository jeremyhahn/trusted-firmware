### Fixed

- **pyOCD's read-back halts the core first** (row `cortex-m`). `savemem` on a
  running STM32H573 read zeros, so `make boot-flash` and `make flash` with
  `CM_FLASH_TOOL=pyocd` failed their read-back; the read now halts the core,
  and pyOCD resumes it on disconnect.

### Changed

- **STM32H573I-DK run on the board** (row `stm32h5`, #535). I2C (the NACK
  path), the EEPROM, PWM, pulse and tone are verified on the board on
  2026-11-17, each with its commit and image SHA-256, in
  `docs/boards/stm32h573i-dk.md`; tfw-boot's own flash writes are not runnable
  (no OTA row), and hardware crypto is not offered. The page gains a "Bench
  run" section and a "Feature matrix", one line per SPEC claim naming the
  board: verified, fails or owner-gated.
