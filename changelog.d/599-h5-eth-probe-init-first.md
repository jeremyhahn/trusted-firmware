### Added

- **A host test checks the STM32H5 ETH_PROBE line inits before it scans**
  (#599). `test_h5_eth_mdio_regs` snapshots the register file at the first
  MDIO command `tfw_h5_eth_probe()` in
  `src/hal/cortex-m/stm32h5/hal_eth_mdio.c` issues: RMII is selected, the MAC
  clocks are on and the MDIO and MDC pins are on their alternate function, so
  a probe that scans before its init fails. Test-only: no firmware changes.
