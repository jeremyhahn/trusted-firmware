### Added

- **The STM32H5 sign yield is tested to sweep the network's deadlines while an
  output is armed** (#599). `test/unit/test_h5_sign_yield.c` yields with an
  output armed but not yet due, and on the yield that releases it: each yield
  still drains the received frames and runs the lwIP timers and lease expiry
  once, drain first (`src/hal/cortex-m/stm32h5/hal_eth_sign_yield.c`). Test
  only; the driver is unchanged.
