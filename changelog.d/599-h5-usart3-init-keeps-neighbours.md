### Added

- **Host tests for the NUCLEO-H563ZI console init's neighbours** (#599).
  `test_stm32h5_uart_usart3` now pins that the console init (USART3,
  `src/hal/cortex-m/stm32h5/hal_uart.c`) keeps the other RCC_APB1LENR
  enables (TIM3, TIM5) and the other GPIO bank clocks on, and GPIOD's other
  pins' mode and alternate function as they were.
  Test-only: no firmware changes.
