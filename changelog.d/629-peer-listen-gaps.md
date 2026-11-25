### Added

- **Tests for the `tcp-listen:` open paths** (row `host-sdk`, #629). New tests
  in `sdk/go/internal/peer` cover three cases. An address that is already bound
  fails at once with an error naming the port, without waiting for a timeout. A
  bare-port `tcp-listen:<port>` listens on every interface. When the reset fails,
  `OpenCmd` releases the listener so the port can be bound again.
