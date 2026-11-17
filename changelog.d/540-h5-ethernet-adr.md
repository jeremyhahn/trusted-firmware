### Added

- **The STM32H573I-DK Ethernet ADR, PROPOSED** (#540).
  `docs/multi-arch/adr-stm32h5-ethernet.md` sets out the IP stack for the
  board's on-chip MAC: (a) lwIP (`NO_SYS`, raw API, its DHCP client) over our
  own MAC, DMA-descriptor and PHY driver behind the Cortex-M HAL, (b) lwIP
  over ST's HAL ETH driver, (c) NetX Duo, (d) a stack of our own. It
  recommends (a), keeps the frames and the allow-list of the
  `ethernet-transport` row unchanged over the device's one TCP connection,
  lists the PHY and RMII pins to confirm on the board, and leaves the choice
  to the owner. The board page links it. No code changes.
