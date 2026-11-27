### Added

- **Host tests for the STM32H5 Ethernet MAC's length and descriptor
  boundaries** (`test_h5_eth_mac`, #599). A 1536-byte frame is sent and
  received whole, a received frame exactly the caller's buffer is copied with
  no byte past it, the later descriptors of a split frame are dropped and
  handed back to the DMA, and init takes only speed and duplex from its
  argument.
