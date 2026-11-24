### Added

- **The ESP32 partition-table test checks the tables against the C sources** (#48).
  `scripts/test_esp32_partitions.py` (added under #23 and run by
  `make scripts-test`) now also checks boards/esp32p4-poe and
  boards/esp32-qemu partitions.csv against the firmware. The labels Layer 0
  opens (PLAT_LABEL, AUTH_LABEL) must each be one data partition. tfw_eeprom
  must hold the board's TFW_EEPROM_SIZE, and the number of ota_N app slots
  must equal TFW_OTA_SLOTS. Every row must end inside TFW_APP_REGION_SIZE,
  and every data row must start on a 4 KB sector.
