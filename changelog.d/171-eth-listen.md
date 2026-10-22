### Fixed

- **The Ethernet rows listen apart from the endpoint the board dials** (#171).
  `make hw-test HW_ETH=0.0.0.0:7020` failed every TestEthernet row before a
  board was opened: the listener bound `[::]:7020` and the rows wrote that into
  the network block, which `NetConfig.MarshalBinary` refuses. `HW_ETH`
  (`TFW_HW_ETH`) is now only the endpoint the board dials, and must name an
  address it can dial; the new `HW_ETH_LISTEN` (`TFW_HW_ETH_LISTEN`) is where
  the suite listens, a wildcard included, and defaults to `HW_ETH`. Port 0
  stays refused for a board. The simulator path is unchanged, and `hw-enroll`,
  which writes no block, needed no change.
