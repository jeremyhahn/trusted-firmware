### Added

- **Host tests for the STM32H5 netif's once-only bring-up failure lines** (#599).
  `test_h5_netif` now pins that the netif says "eth: not provisioned" and
  "eth: rng failed" once each over four bring-up retries, not once per retry
  (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test-only: no firmware changes.
