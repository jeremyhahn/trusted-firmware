### Added

- **A host test pins that the STM32H5 EEPROM takes a confirm request only at
  its own length** (`test_stm32h5_eeprom`, #599). A write at the confirm
  request's offset one byte short, one byte long or running to the end of
  tfw-boot's span is refused and stores nothing, so no write there reaches
  tfw-boot's bytes after the request.
