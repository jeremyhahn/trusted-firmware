### Added

- test/unit/test_esp_uart_mutate.c pins the ESP32 wire's boot hold at TX_BUF: with no host exactly 512 bytes wait for the first host, and the 513th byte discards them whole and ends the hold, so no byte is written past the buffer and none after the discard is held again (Refs #48).
