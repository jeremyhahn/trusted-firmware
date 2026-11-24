### Added

- test/unit/test_esp_uart_mutate.c pins the ESP32 wire's full-buffer send failure: the byte `tfw_hal_uart_tx_try` is offered when the full buffer's send fails goes with the dropped host and never reaches the next one (Refs #48).
