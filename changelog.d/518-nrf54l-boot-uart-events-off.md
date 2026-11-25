### Added

- **Host tests for tfw-boot's nRF54L15 uart events and off** (#518). Over the
  nRF54L register model, `make host-test` now checks that init drops a stale
  RX.END, that a send waits for TXDRDY and clears its events, that a send whose
  TXDRDY never comes still returns, and that after off a receive, a send and a
  second off do nothing while the console TX pin is driven low.
