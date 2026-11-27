### Added

- **Host tests for a zero-length STM32H5 EEPROM read or write** (#599,
  `src/hal/cortex-m/stm32h5/hal_eeprom.c`).
  `test_stm32h5_eeprom` now pins that a zero-length read or write at any
  address up to TFW_EEPROM_SIZE is TFW_OK, fills no byte of the caller's
  buffer and runs no flash operation, while one past the end, and a
  zero-length write into tfw-boot's platform window, stay TFW_ERR_RANGE.
  Test-only: no firmware changes.
