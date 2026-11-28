### Added

- **Host tests for the STM32H5 netif's bring-up retry period** (#599).
  `test_h5_netif` now pins that the netif tries bring-up again 1000 ms
  (`RNG_RETRY_MS`) after a failed network-block load and after a failed RNG
  draw, not at 999 ms and not 100 ms late
  (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test-only: no firmware changes.
