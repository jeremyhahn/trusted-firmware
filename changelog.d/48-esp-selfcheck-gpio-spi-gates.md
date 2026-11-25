### Added

- test/unit/test_esp_selfcheck.c tests that a refused later GPIO read or write stops the P4 self-check's GPIO check at that step, and that a refused SPI chip-select set-up transfers nothing and still releases the pin (Refs #48).
