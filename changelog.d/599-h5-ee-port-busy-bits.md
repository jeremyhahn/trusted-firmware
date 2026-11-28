### Added

- **Host tests for the STM32H5 EEPROM flash port's busy bits** (#599).
  `test_stm32h5_ee_flash_port` now pins that the port's wait
  (`src/hal/cortex-m/stm32h5/ee_flash_port.c`) holds on FLASH_NSSR bits 0,
  1 and 3 (BSY, WBNE, DBNE per RM0481) by value, and leaves at once with
  every other bit set. Test-only: no firmware changes.
