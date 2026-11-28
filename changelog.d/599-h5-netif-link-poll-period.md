### Added

- **Host tests for the STM32H5 netif's PHY link poll period** (#599).
  `test_h5_netif` now pins that the netif reads the PHY's link 100 ms
  (LINK_POLL_MS) after bring-up and 100 ms after each poll, not at 99 ms and
  not 100 ms late, for a link coming up and for a link lost
  (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test-only: no firmware changes.
