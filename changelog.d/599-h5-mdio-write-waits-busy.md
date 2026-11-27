### Added

- **Host tests for the STM32H5 MDIO write's busy wait** (#599).
  `test_h5_eth_mdio_regs` now pins that `tfw_h5_eth_mdio_write`
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) waits out a command still busy
  on MACMDIOAR.MB before it issues its own, and gives up only after the full
  100000-poll spin limit. Test-only: no firmware changes.
