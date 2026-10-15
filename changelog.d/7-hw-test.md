### Added

- **`make hw-test BOARD=<board> PORT=<dev>`** (#7) runs the SDK's
  cross-implementation suite, the one `make sdk-test` runs against `tfwsim`,
  against a flashed board on a serial port. The suite gets its connection
  through one seam, `sdk/go/internal/peer`. That seam resets the board by
  pulsing DTR and waits for its BOOT event. The suite checks the pins of the
  board it is told it has, and `CAPS` must report that board. A missing or
  silent port fails the run. The target is not part of `make ci`.
