### Added

- **An ESP32 OTA_INFO with no running partition answers an internal error**
  (row `ota`, #618). `test/unit/test_esp_ota.c` pins that when ESP-IDF finds
  no running partition OTA_INFO answers `TFW_ERR_INTERNAL` for either slot,
  never a made-up state for a device that cannot name the image it runs.
