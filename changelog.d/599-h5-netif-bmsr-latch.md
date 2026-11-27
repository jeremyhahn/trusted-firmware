### Added

- **Host tests for the STM32H5 netif's latched BMSR link read** (#599).
  `test_h5_netif` now pins that the link poll reads BMSR twice and takes the
  link from the second read, so a first read whose link bit latched low
  neither keeps a coming-up link down nor stops a running MAC
  (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test-only: no firmware changes.
