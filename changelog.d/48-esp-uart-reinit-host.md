### Added

- test/unit/test_esp_uart_mutate.c pins the ESP32 wire's re-init with a host connected: `tfw_hal_uart_init` keeps that host's unsent reply and holds nothing for the next host (Refs #48).
