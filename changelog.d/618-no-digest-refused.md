### Added

- **An ESP32 OTA_ACTIVATE of an image with no appended digest is refused**
  (row `ota`, #618). `test/unit/test_esp_ota.c` pins that an image ESP-IDF
  reports without its appended SHA-256 gets `TFW_ERR_RANGE` and writes neither
  a record nor the boot slot: its trailer is not where layer 0 reads it.
