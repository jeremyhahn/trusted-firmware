### Added

- **Host tests for tfw-boot's STM32H5 UART init at a baud the clock
  cannot divide** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that a non-zero baud whose divisor falls
  outside 16..0xFFFF (8 Mbaud and 900 baud at 64 MHz) leaves clocks, pins
  and the USART as reset, so transmit stores nothing and the hand-over
  leaves neighbouring clocks and pins alone. Test-only: no firmware
  changes.
