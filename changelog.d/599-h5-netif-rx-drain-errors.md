### Added

- **Host tests for the STM32H5 netif's RX drain and BMSR read errors** (#599).
  `test_h5_netif` now pins that `src/hal/cortex-m/stm32h5/hal_eth_netif.c`
  keeps draining the RX ring past a frame the MAC dropped and past a frame
  an empty pbuf pool cannot hold, so the frames behind them are answered in
  the same poll, and that a failed BMSR read takes a running link down
  (MAC stopped, netif link down) until a good read brings it back.
  Test-only: no firmware changes.
