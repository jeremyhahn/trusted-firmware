### Added

- **Host tests for an STM32H5 EEPROM read inside tfw-boot's platform
  window** (#599, `src/hal/cortex-m/stm32h5/hal_eeprom.c`).
  `test_stm32h5_eeprom` now pins that a read lying wholly inside the window,
  the boot confirm request's read-back included, returns the window's bytes
  and writes nothing past the caller's length.
  Test-only: no firmware changes.
