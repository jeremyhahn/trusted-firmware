### Added

- test/unit/test_esp_selfcheck.c pins the order of the ESP32 bench self-check's set-up and tear-down calls on a good board: the gpio pin goes to output and back to input, the ADC is set up before its read, each LEDC duty read waits 20 ms after its write, LEDC is stopped, the pulse pin is pulled up and SPI is set up before its transfer, so a dropped or misplaced call fails the suite (Refs #48).
