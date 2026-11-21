### Added

- **The NUCLEO-H563ZI console refuses an out-of-range baud untouched** (#599).
  `test_stm32h5_uart_usart3` checks that a baud of 0, one whose divisor is under
  16 and one whose divisor is over 0xFFFF each return `TFW_ERR_RANGE` with no
  USART3, RCC, GPIOD or NVIC register written, and that a 115200 init after a
  refusal still programs the BRR.
