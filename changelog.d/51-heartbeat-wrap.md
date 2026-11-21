### Added

- **A host test pins the example heartbeat across the millisecond wrap** (#51).
  The example application's 500 ms heartbeat keeps its period when the 32-bit
  millisecond counter wraps: a beat scheduled past the wrap waits its full
  500 ms and does not come early.
