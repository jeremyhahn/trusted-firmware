### Added

- **A host test pins the STM32H5 MAC's start and stop against init's setup**
  (`test_h5_eth_mac`, #599). Start clears the DMA status (DMACSR
  write-1-clear), and start and stop change only their own enable bits: the
  speed, duplex, CRC strip, DMA burst length and RX buffer size that
  init wrote stay as they were, at both 100M full and 10M half duplex.
