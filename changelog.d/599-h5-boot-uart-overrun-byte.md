### Added

- **Host tests for the STM32H5 boot UART's overrun byte and divisor floor**
  (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`). `test_boot_h5_uart` now
  pins that a byte waiting beside an overrun is taken while the overrun is
  cleared, and that a baud too fast for its divisor (below 16) leaves the
  USART, its clock and its pins untouched. Test-only: no firmware changes.
