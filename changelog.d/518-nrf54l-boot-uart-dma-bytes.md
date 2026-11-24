### Added

- **Host tests for the byte tfw-boot's nRF54L15 uart hands out and the byte
  and buffer it sends** (#518). The test now includes boot_uart.c, so it checks
  that rx hands out the byte EasyDMA wrote into the armed buffer, that rx with
  nothing received (or after off) leaves the caller's byte alone, and that tx
  puts its byte in the transmit buffer TX.PTR points at.
