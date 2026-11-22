### Added

- test/unit/test_esp_selfcheck.c pins the ESP32 bench self-check's EEPROM read-back compare: a pattern write that reports OK but reads back wrong fails `hal_eeprom`, so a store that drops a byte never prints PASS on its status alone (Refs #48).
