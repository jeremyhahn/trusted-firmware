### Added

- **A host test pins that the STM32H5 EEPROM's refusals leave everything
  untouched** (`test_stm32h5_eeprom`, #599). A refused read writes no byte into
  the caller's buffer, a refused write past the end touches no flash, and a
  confirm request at the wrong offset is refused and stores nothing.
