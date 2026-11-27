### Fixed

- **A connected Ethernet peer that stops reading no longer holds serial** (Refs #55). `src/proto/link_eth.c` closes the connection and drops the reply when the peer has taken none of it for `TFW_LINK_ETH_TX_STALL_MS` (2 s), as the STM32H5 wire does (#584), and redials after the usual backoff; before, `tfw_eth_tx()` refused each byte for as long as the peer held the socket and the transport read nothing from serial.
