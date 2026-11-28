### Added

- **The STM32H5 Ethernet PHY scan is tested on a bus held low and a failed
  second BMSR read** (#599). `test/unit/test_h5_eth_mdio.c` scans a bus whose
  every register reads 0x0000: no PHY is found, and a PHY at address 6 is
  found past the zeros; and a PHY whose first BMSR read says link up but whose
  second read fails is reported link down
  (`src/hal/cortex-m/stm32h5/hal_eth_mdio.h`). Test only; the driver is
  unchanged.
