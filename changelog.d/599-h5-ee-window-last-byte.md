### Added

- **Host tests for an STM32H5 EEPROM read starting at the platform window's
  last byte** (#599, `src/hal/cortex-m/stm32h5/hal_eeprom.c`).
  `test_stm32h5_eeprom` now pins that a read starting at 0x13F returns that
  byte from tfw-boot's window, whether it is read alone or with the user
  sector's first byte after it, and writes nothing past the caller's length.
  Test-only: no firmware changes.
