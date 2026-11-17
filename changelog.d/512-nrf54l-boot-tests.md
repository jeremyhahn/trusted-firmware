### Added

- **Host tests for tfw-boot's nRF54L15 UART and RRAM write** (#512). Over the
  nRF54L register model, `make host-test` now checks that `boot_uart.c` leaves
  the UARTE's interrupts off, moves one byte per transfer, and stops the armed
  receive and puts the UARTE and console pins back to their reset values
  before the jump; and that `boot_rram.c` sets RRAMC CONFIG.WEN only inside
  the write, refuses a span that starts below tfw-boot's region end, and fails
  the write with `TFW_ERR_TIMEOUT` when the controller never answers.
