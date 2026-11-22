### Changed

- STM32H573I-DK `ETH_MAC=1` image (#584): the device opens one outbound TCP
  connection to the endpoint in its network block and listens on nothing; the
  inbound listener on port 21574 is removed. A refused or dropped connection
  is redialled after 1 s, the wait doubling per refusal up to 16 s. The
  address, mask, gateway and MAC come from the network block; DHCP runs only
  when the block asks for it; the UID-derived MAC is used only while the
  block's MAC is all zero; with no valid block the device stays off the
  network and serves the UART. The 2026-11-17 and 2026-11-18 TCP board rows
  were of the inbound listener and are to re-run.
