### Added

- **A host test pins that a missing PHY or a MAC reset timeout keeps the
  STM32H5 netif down for the boot** (`test_h5_netif`, #599). The failure is
  said once, bring-up is not retried, and lwIP is never polled.
