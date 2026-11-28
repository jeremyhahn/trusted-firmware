### Added

- **Host tests for the STM32H5 MDIO write's fresh command** (#599).
  `test_h5_eth_mdio_regs` now pins that `tfw_h5_eth_mdio_write`
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) stores a whole new MACMDIOAR
  command, so a write after a read or after another write carries only its
  own PHY address, register and write opcode, never the previous command's.
  Test-only: no firmware changes.
