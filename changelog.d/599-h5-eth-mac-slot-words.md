### Added

- **A host test pins the STM32H5 Ethernet descriptor reuse** (#599).
  `test/unit/test_h5_eth_mac.c` sends six frames round the four-slot TX ring:
  each lands in its own slot's buffer and a reused slot's DES0/DES1 write-back
  is replaced; and an RX descriptor re-armed after a frame, a dropped frame or
  an init has DES1 and DES2 cleared (`src/hal/cortex-m/stm32h5/hal_eth_mac.c`).
  Test only; the driver is unchanged.
