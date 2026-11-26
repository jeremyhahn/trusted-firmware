### Added

- **A host test pins the STM32H5 EEPROM write's protected-region status**
  (#599): a locked or write-protected flash interface makes the write
  TFW_ERR_PROTECTED_REGION, not TFW_ERR_INTERNAL, and leaves the user sector.
