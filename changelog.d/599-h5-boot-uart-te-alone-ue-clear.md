### Added

- **Host tests for tfw-boot's STM32H5 UART hand-over and tx gating on TE
  alone** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that the hand-over (`tfw_cm_boot_uart_off`)
  and tx gate on TE alone, with UE not consulted: a USART with TE set and
  UE clear is put back to reset by the hand-over (CR1, divisor, PA9/PA10
  and the USART1 clock), and tx stores its byte in TDR. Test-only: no
  firmware changes.
