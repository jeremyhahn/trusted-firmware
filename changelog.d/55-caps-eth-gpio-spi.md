### Added

- **An Ethernet image's GPIO and SPI refusals on the shield's pins are tested** (#55):
  `test/unit/test_caps_eth.c` proves that on the ETH=1 pin table PIN_MODE and
  DIGITAL_WRITE refuse D10-D13 as reserved and drive nothing, SPI_XFER refuses
  D10 as a chip-select before the bus is touched, and DIGITAL_READ still answers
  on D12.
