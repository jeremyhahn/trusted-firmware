### Added

- **Host tests for tfw-boot's STM32H5 UART hand-over** (#599).
  `test_boot_h5_uart` now covers `tfw_cm_boot_uart_off`, which runs before
  every jump to the application: USART1 and PA9/PA10 go back to their reset
  values with the other clocks and pins kept, an init left off is not touched,
  and a tx after the hand-over stores nothing.
