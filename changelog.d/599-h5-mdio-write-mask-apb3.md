### Added

- **Host tests for the STM32H5 MDIO write mask and APB3 clock enable** (#599).
  `test_h5_eth_mdio_regs` now pins that `tfw_h5_eth_mdio_write`
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) masks a PHY address and
  register to five bits each as the read does, and that
  `tfw_h5_eth_mdio_init` sets SBSEN while keeping the other APB3 clocks on.
  Test-only: no firmware changes.
