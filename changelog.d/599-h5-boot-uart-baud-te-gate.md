### Added

- **Host tests for tfw-boot's STM32H5 UART baud and transmit gate**
  (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that init writes the divisor of the baud it
  is given (9600 baud at 64 MHz gives BRR 0x1A0B, and a second init at
  115200 replaces it with 0x22C), and that tx stores nothing while TE is
  clear even with UE and RE set. Test-only: no firmware changes.
