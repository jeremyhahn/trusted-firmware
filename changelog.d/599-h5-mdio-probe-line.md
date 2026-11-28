### Added

- **Host tests for the STM32H5 Ethernet probe's boot line** (#599).
  `test_h5_eth_mdio_regs` now pins that `tfw_h5_eth_probe`
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) runs the MDIO init first,
  reports the first PHY that answers as
  `eth: phy addr=N id=XXXX:XXXX link=up|down`, and writes `eth: phy none`
  after all 32 addresses read idle. Test-only: no firmware changes.
