### Added

- **Host test for the nRF54L15 uart's full receive ring** (#518). Over the
  nRF54L register model, `make host-test` now checks that a byte arriving on a
  full 63-byte receive ring is dropped, not written over the oldest, and that
  the next one-byte receive still starts.
