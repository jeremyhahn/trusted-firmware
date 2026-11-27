### Added

- **Host tests for tfw-boot's STM32H5 UART init over a USART left
  running** (#599, `boot/hal/cortex-m/stm32h5/boot_uart.c`).
  `test_boot_h5_uart` now pins that init replaces the old divisor in BRR
  rather than OR-ing into it, and leaves CR1 at exactly UE, RE and TE,
  clearing any RXNEIE/TXEIE left set, so tfw-boot enables no interrupt.
  Test-only: no firmware changes.
