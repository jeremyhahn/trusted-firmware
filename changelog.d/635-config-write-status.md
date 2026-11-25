### Added

- **A test that `tfw_app_config_write` returns the HAL's EEPROM status**
  (#635). `include/tfw/app.h` promises it, as it does for
  `tfw_app_config_read`, but only the read half had a test. The host mock
  gains `tfw_mock_eeprom_write_fails`, a bus that refuses every write and
  stores and counts no byte, and `test_app_state_wire` checks a refused
  write returns `TFW_ERR_TIMEOUT` and leaves the config region erased.
