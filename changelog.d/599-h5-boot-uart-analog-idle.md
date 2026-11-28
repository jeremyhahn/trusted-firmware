### Added

- **Host tests for tfw-boot's STM32H5 UART init over analog pins and rx
  on an idle transmitter** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that init replaces PA9/PA10's mode and AF
  fields when they start analog (as the part resets them), leaving every
  other pin as it was, and that rx with only TXE and TC set reports no byte
  and leaves the caller's byte and ICR alone. Test-only: no firmware
  changes.
