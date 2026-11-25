### Added

- **Host test for a second nRF54L15 uart init** (#518). Over the nRF54L
  register model, `make host-test` now checks that a second
  `tfw_hal_uart_init` drops the received bytes nobody read, the queued bytes
  and a byte stalled on the wire, and that the next send starts at once.
