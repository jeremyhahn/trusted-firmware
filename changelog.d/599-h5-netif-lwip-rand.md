### Added

- **The STM32H5 netif's LWIP_RAND is tested from its seed** (#599).
  `test/unit/test_h5_netif.c` binds a TCP pcb to port 0 after bring-up and
  checks the port the RNG-seeded generator gives, and checks
  tfw_h5_eth_rand's xorshift32 draws from its boot state and after the RNG
  seed replaces it (`src/hal/cortex-m/stm32h5/hal_eth_netif.c`). Test only;
  the driver is unchanged.
