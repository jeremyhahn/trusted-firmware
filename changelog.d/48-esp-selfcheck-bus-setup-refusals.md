### Added

- test/unit/test_esp_selfcheck.c tests that the ESP32-P4 self-check's I2C, SPI and 1-Wire checks FAIL and stop on a refused init, an unresolved pin, or a refused 1-Wire reset or read, and never release a CS pin the board never resolved (Refs #48).
