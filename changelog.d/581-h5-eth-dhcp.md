### Added

- **STM32H573I-DK Ethernet frames and a DHCP lease** (row `stm32h5`, #581,
  slice 2 of the Ethernet ADR, #540). `src/hal/cortex-m/stm32h5/hal_eth_mac.c`
  drives the MAC and DMA in poll mode: four TX and four RX descriptors of 1536
  bytes, OWN hand-offs and tail pointers, the speed and duplex from the
  LAN8742A's PSCSR, a locally administered MAC address from the device UID;
  host-tested by `test/unit/test_h5_eth_mac.c`. lwIP 2.2.1
  (`STABLE-2_2_1_RELEASE`) is vendored unmodified under `third_party/lwip`
  (commit and tree hash in its `VERSION`), run `NO_SYS` with the raw API from
  the main loop through `hal_eth_netif.c`. Built with `ETH_MAC=1` (off by
  default; `ETH=1` stays the W5x00 link and the default image is unchanged;
  the board's RAM budget is 64 KiB under it). Run on the board 2026-11-17: the
  console printed `eth: dhcp bound` and the leased address. tfw-wire over TCP
  is the next slice.
