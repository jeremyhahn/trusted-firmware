### Added

- **Host tests for tfw-boot's nRF54L15 uart receive** (#518). Over the
  nRF54L register model, `make host-test` now checks that tfw-boot's uart
  hands a received byte out once, clears a line error (ERROR and ERRORSRC) at
  the next poll, and leaves the UARTE off, reading no byte, when init is given
  a rate the vendor's table does not list.
