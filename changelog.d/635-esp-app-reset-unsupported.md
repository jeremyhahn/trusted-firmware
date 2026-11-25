### Added

- **The ESP32 application link's suite checks that RESET(1) is refused**
  (#635). `test_esp_app_ota` now asserts that RESET(1) returns
  `TFW_ERR_UNSUPPORTED` with no reset pending and no tfw-boot session, and
  that a plain RESET(0) is still accepted.
