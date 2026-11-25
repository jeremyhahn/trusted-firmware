### Added

- **A W5x00 socket in a TCP closing state is tested as down and closed** (#55):
  test_w5100 and test_w5500 set socket 0 to SOCK_FIN_WAIT, SOCK_CLOSING,
  SOCK_TIME_WAIT and SOCK_LAST_ACK in turn, with a reply queued, and check
  that tfw_eth_state() reports TFW_ETH_DOWN, issues a CLOSE, and that the
  queued reply is dropped rather than sent. A driver that read a closing state
  as busy and left the connection open passed every test before.
