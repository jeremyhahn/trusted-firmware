### Added

- test/unit/test_esp_uart_mutate.c pins the ESP32 wire's re-init hold reset: a `tfw_hal_uart_init` with no host is boot, so the bytes the last boot held for the first host are dropped and that host gets only this boot's output, never two boots' output run together (Refs #48).
