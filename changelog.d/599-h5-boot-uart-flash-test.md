### Added

- **Host tests for tfw-boot's STM32H5 UART and flash entry points** (#599).
  `test_boot_h5_uart` runs `boot/hal/cortex-m/stm32h5/boot_uart.c` under the
  STM32H573I-DK's profile over the register file: a zero baud divisor writes
  nothing, init enables USART1 with its receiver and transmitter and puts PA9
  and PA10 on AF7, rx clears an overrun, and tx stores nothing while the
  transmitter is off. `test_boot_h5_flash_entry` covers `boot_flash.c`: an
  address below the flash is out of range, the driver gets the offset from the
  flash's first byte, and the wait spins while BSY, WBNE or DBNE is set. Both
  run in `make boot-test` and `make host-test`.
