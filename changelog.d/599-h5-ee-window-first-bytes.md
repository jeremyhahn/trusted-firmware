### Added

- **Host tests for an STM32H5 EEPROM read from address 0** (#599,
  `src/hal/cortex-m/stm32h5/hal_eeprom.c`). `test_stm32h5_eeprom` now pins
  that a read starting at address 0 returns tfw-boot's window from its first
  byte, for 4 bytes and for the full 255-byte length, and writes nothing past
  the caller's length. Test-only: no firmware changes.
