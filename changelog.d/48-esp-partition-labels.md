### Added

- **A host check of the ESP32 partition tables** (#48).
  `scripts/test_esp32_partitions.py`, run by `make scripts-test`, checks
  boards/esp32p4-poe and boards/esp32-qemu partitions.csv against the
  firmware: every tfw_* label the firmware and Layer 0 look up is one data
  partition, tfw_eeprom holds TFW_EEPROM_SIZE, the ota_N app slots match
  TFW_OTA_SLOTS, and the rows are aligned, disjoint and inside
  TFW_APP_REGION_SIZE.
