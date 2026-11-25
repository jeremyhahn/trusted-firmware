### Added

- **The nRF54L TWIM test pins an RX overrun as TFW_ERR_INTERNAL, not a NACK**
  (#518). The model raises ERRORSRC.OVERRUN on a read, alone and after a write
  byte; the transfer still stops and the error latch is cleared, so a driver
  that called every bus error a NACK, and a caller retrying it, now fails.
