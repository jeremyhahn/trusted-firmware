### Added

- **The STM32H5 console USART's overrun clear is tested beside a full ring and
  a send** (#599). `test/unit/test_stm32h5_uart.c` raises an overrun with the
  RX ring full, which drops the byte and keeps the 63 queued in order yet still
  clears ORE, and an overrun beside TXE, which clears ORE and sends the next
  queued byte with TXEIE kept on (`src/hal/cortex-m/stm32h5/hal_uart.c`). Test
  only; the driver is unchanged.
