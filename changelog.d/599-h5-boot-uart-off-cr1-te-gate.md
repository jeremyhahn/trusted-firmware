### Added

- **Host tests for tfw-boot's STM32H5 UART hand-over clearing CR1 whole
  and its TE gate** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that the hand-over (`tfw_cm_boot_uart_off`)
  leaves CR1 at its reset value 0 even with RXNEIE and TXEIE set after
  init, and that it is gated on TE alone, so a USART with UE and RE set but
  TE clear keeps its CR1, divisor, pins and clocks. Test-only: no firmware
  changes.
