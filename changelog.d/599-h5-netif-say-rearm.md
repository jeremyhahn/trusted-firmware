### Added

- **Host tests for when the STM32H5 netif's bring-up failure lines re-arm** (#599).
  `test_h5_netif` now pins that the netif says "eth: not provisioned" again
  when a block that loaded goes missing, and does not say "eth: rng failed"
  again after a missing-block interlude between two failed draws
  (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test-only: no firmware changes.
