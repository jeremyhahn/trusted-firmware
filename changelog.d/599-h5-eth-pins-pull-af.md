### Added

- **Host tests for the STM32H5 Ethernet pin init's pulls and alternate
  functions** (#599). `test_h5_eth_mdio_regs` now pins that
  `tfw_h5_eth_mdio_init` (`src/hal/cortex-m/stm32h5/hal_eth_mdio.c`) clears a
  pull left on each RMII pin and replaces a stale alternate function with
  AF11, keeping every other pin's pull and alternate function on GPIOA, GPIOC
  and GPIOG. Test-only: no firmware changes.
