### Added

- **The W5500 frame suite pins socket 1's block selects** (#55):
  `test/unit/test_w5500_spi_mutate.c` checks socket 1's registers, TX buffer
  and RX buffer frames (BSB 00101, 00110, 00111) against the datasheet's
  control-byte literals, as it already did for socket 0, so a chip file that
  aliased the DHCP client's socket onto socket 0's blocks now fails it.
