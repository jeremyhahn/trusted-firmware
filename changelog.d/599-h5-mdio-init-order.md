### Added

- **The STM32H5 MDIO init is tested to write in order** (#599).
  `test/unit/test_h5_eth_mdio_regs.c` looks at the register file before every
  access init makes: RMII is selected in SBS_PMCR before any MAC clock is on,
  and the SBS clock is on before SBS_PMCR is written
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`). Test only; the driver is
  unchanged.
