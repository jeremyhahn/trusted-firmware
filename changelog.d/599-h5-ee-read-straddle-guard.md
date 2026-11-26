### Added

- **A host test proves an STM32H5 EEPROM read across the end of tfw-boot's span
  writes only the caller's bytes** (#599). A 4-byte read that joins the window
  and the user sector fills its 4 bytes and leaves the bytes past them in the
  caller's buffer untouched.
