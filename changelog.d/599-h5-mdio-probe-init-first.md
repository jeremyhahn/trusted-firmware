### Added

- **Host tests for the STM32H5 Ethernet probe's init order** (#599).
  `test_h5_eth_mdio_regs` now pins that `tfw_h5_eth_probe`
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) has selected RMII, turned the
  MAC clocks on and put the MDIO/MDC pins on their alternate function before
  its first MDIO command, not only by the time it returns. Test-only: no
  firmware changes.
