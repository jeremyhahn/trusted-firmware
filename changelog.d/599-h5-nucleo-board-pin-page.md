### Added

- **The NUCLEO-H563ZI's descriptor test reads its CAPS pin page** (#599).
  `test_board_nucleo-h563zi` checks each LED's six-byte page: the pin, its
  digital and LED flags, the board's port index (0, 1, 2 for GPIOB, GPIOF,
  GPIOG, not the bank `tfw_board_pin` resolves), the bit, no converter and no
  compare unit. A pin past the board gets `TFW_ERR_BAD_PIN` with the page left
  unwritten, no converter or compare unit, and no flags or PWM state. A pin
  page that reports a hardcoded port now fails the test.
