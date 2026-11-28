### Added

- **Host tests for the STM32H5 Ethernet PHY scan's ID read failures and the
  boot line's digits** (#599, `src/hal/cortex-m/stm32h5/hal_eth_mdio.h`).
  `test_h5_eth_mdio` now pins that the scan skips an address when only its
  PHYID1 or only its PHYID2 read fails (and stops reading it after a failed
  PHYID1), and that the boot line prints address 9 with one digit, 10 with
  two, and the identifier's hex digits 0-9 and A-F.
  Test-only: no firmware changes.
