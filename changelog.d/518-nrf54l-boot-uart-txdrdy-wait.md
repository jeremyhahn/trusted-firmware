### Added

- **The nRF54L boot UART transmit test fails if the driver stops waiting for
  TXDRDY before it clears the event** (#518). After TX.START the test sees
  TXDRDY used twice, by the wait and then by the cleanup, and first raised on
  the wait's use.
