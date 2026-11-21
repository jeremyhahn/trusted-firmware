### Added

- **A host test runs the real STM32H5 MDIO driver** (#599).
  `test_h5_eth_mdio_regs` builds `src/hal/cortex-m/stm32h5/hal_eth_mdio.c` on
  the Cortex-M register file, with MACMDIOAR.MB clearing after a set number of
  polls, and checks the read and write commands (PHY, register, GOC, MB), the
  data register, the spin limit failing both, and init's RMII select, MAC
  clocks and AF11 pins. The values are the driver's, not yet confirmed against
  RM0481 or run on a board.
