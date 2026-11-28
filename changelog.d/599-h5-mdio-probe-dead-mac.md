### Added

- **Host tests for the STM32H5 Ethernet probe on an MDIO that never
  completes** (#599). `test_h5_eth_mdio_regs` now pins that
  `tfw_h5_eth_probe` (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) then prints
  `eth: phy none`, not a PHY made from the stale data register, gives up
  after one spin limit per address, and writes no command over one still in
  flight. Test-only: no firmware changes.
