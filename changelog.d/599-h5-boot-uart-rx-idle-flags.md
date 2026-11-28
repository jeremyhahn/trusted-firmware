### Added

- **Host tests for tfw-boot's STM32H5 UART receive with the idle TX flags
  set** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that receive takes RDR with the
  transmitter's idle flags TXE and TC set alongside RXNE, and clears an
  overrun (ICR ORECF) that arrives together with a byte while still
  returning that byte. Test-only: no firmware changes.
