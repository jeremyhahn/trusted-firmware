### Added

- **STM32H573I-DK Ethernet PHY probe** (row `stm32h5`, #579, slice 1 of the
  Ethernet ADR, #540). `src/hal/cortex-m/stm32h5/hal_eth_mdio.c` selects RMII,
  turns on the MAC's clocks, routes the nine RMII pins on alternate function 11,
  reads and writes the PHY over MDIO at 1.52 MHz, scans addresses 0–31 and
  reads PHYID1, PHYID2 and BMSR (twice: the link bit latches low). Built with
  `ETH_PROBE=1` (off by default; the default image is unchanged), the board
  prints `eth: phy addr=N id=XXXX:XXXX link=up|down` at boot. Run on the board
  2026-11-17: a LAN8742A (`0007:C131`) at address 0, link up. No MAC data path
  or IP stack yet.
