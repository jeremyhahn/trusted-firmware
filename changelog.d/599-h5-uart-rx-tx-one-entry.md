### Added

- **One STM32H5 console UART interrupt with a received byte and TXE is tested
  to serve both** (#599). `test/unit/test_stm32h5_uart.c` enters the USART1
  handler with RXNE and TXE set together: the byte is queued and the next
  queued byte goes out, or on an empty TX ring TXEIE is turned off with no TDR
  write (`src/hal/cortex-m/stm32h5/hal_uart.c`). Test only; the driver is
  unchanged.
