### Added

- **Host tests for one STM32H5 ETH_MAC sign yield** (#599).
  `test_h5_sign_yield` now pins that one `tfw_sign_yield`
  (`src/hal/cortex-m/stm32h5/hal_eth_sign_yield.c`) runs the received-frame
  drain and the deadline sweep once each, and that an output due at the
  yield is released before the network is serviced.
  Test-only: no firmware changes.
