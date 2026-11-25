### Added

- **A W5x00 socket still at INIT is tested as busy and kept open** (#55):
  test_w5100_robust and test_w5500_robust set socket 0 to SOCK_INIT after
  tfw_eth_start() and check that tfw_eth_state() reports TFW_ETH_BUSY and
  leaves the socket open, then that it reads up once the peer accepts and the
  next frame is read whole. A driver that reported INIT as up, or closed it as
  a hang-up, passed every test before.
