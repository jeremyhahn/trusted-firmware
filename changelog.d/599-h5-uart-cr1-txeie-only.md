### Added

- **Host tests for the STM32H5 console's CR1 and clock-enable writes** (#599,
  `src/hal/cortex-m/stm32h5/hal_uart.c`). `test_stm32h5_uart` now pins that
  a send and the empty-ring service routine change only TXEIE in USART CR1
  (UE, RE, TE and RXNEIE stay on), and that init keeps the other
  RCC_AHB2ENR and RCC_APB2ENR clock enables. Test-only: no firmware changes.
