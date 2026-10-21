### Added

- **The ESP32 EEPROM HAL runs on the host** (#48, row `esp32-p4`).
  `test_esp_hal_eeprom` links `src/hal/esp32/hal_eeprom.c` against the
  esp32p4-poe profile over mock NOR flash partitions of `partitions.csv`'s
  sizes: a span past the end is `TFW_ERR_RANGE` and touches nothing, a write
  rewrites only the 4 KB sectors whose bytes change, an unchanged write erases
  nothing, no partition is `TFW_ERR_UNSUPPORTED`, and a refused read, erase or
  write is `TFW_ERR_INTERNAL`.
