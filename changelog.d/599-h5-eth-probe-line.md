### Added

- **Host tests pin the STM32H5 ETH_PROBE boot line** (#599).
  `test_h5_eth_mdio_regs` runs `tfw_h5_eth_probe()` from
  `src/hal/cortex-m/stm32h5/hal_eth_mdio.c` over the MDIO register model: the
  init runs first (RMII selected, MAC clocks on), a PHY is named with its
  address, id and link, an all-0xFFFF bus and a hung MDIO both say
  "eth: phy none", and a buffer too small for the line gives nothing.
