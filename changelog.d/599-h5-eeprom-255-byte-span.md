### Added

- **The STM32H5 EEPROM is tested at its longest write** (#599).
  `test/unit/test_stm32h5_eeprom.c` writes 255 bytes across 17 quad-words and
  reads them back with the bytes either side left erased, and rewrites 255
  bytes ending on the user sector's last byte, changed only in its last
  quad-word, with one erase, while the same write one byte later is refused
  (`src/hal/cortex-m/stm32h5/hal_eeprom.c`). Test only; the driver is
  unchanged.
