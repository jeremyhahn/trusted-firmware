### Added

- **A host test pins the STM32H5 Ethernet RX bounds** (#599). A frame the
  driver drops (zero length, past the caller's cap, past the 1536-byte DMA
  buffer, an error summary or a split frame) copies no byte into the caller's
  buffer and re-arms its descriptor, buffer address and tail pointer included.
