### Added

- **A host test of the Cortex-M `tfw_hal_nvm_read` window bound** (#628).
  `test_cm_hal_eeprom` reads both ends of the platform window and shows that a
  span one byte past its end, or one that would wrap the 16-bit address, is
  `TFW_ERR_RANGE` with the buffer untouched.
