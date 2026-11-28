### Added

- **Host tests for the STM32H5 Ethernet MAC init's DMA and filter words** (#599).
  `test_h5_eth_mac` now pins that MAC init writes the DMA bus mode (fixed
  bursts, address-aligned beats) and the TX burst length, and clears the
  descriptor skip and the frame filter, even over stale register values
  (`src/hal/cortex-m/stm32h5/hal_eth_mac.c`). Test-only: no firmware changes.
