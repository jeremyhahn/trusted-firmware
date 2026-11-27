### Fixed

- The ESP32 wire's tfw_hal_uart_init closes the listener the last init made before making the next, so a re-init no longer leaks a socket or fails to listen on the wire port (Refs #48).
