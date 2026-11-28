### Added

- **Host tests for tfw-boot's STM32H5 UART tx waiting on TXE alone**
  (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that tx (`tfw_boot_uart_tx`) waits on the
  TXE bit alone: it stores its byte in TDR with the idle transmitter's TC
  flag also set, and with a received byte and an overrun waiting, which it
  leaves for rx (RDR unread, no ICR write). Test-only: no firmware changes.
